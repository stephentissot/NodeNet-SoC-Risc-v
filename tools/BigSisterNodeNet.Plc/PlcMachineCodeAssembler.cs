using System;
using System.Collections.Generic;
using System.IO;

namespace BigSisterNodeNet.Plc
{
    public sealed class PlcMachineCodeCompileException : Exception
    {
        public PlcMachineCodeCompileException(string message, int lineNumber)
            : base($"Line {lineNumber}: {message}")
        {
            LineNumber = lineNumber;
        }

        public int LineNumber { get; }
    }

    public static class PlcMachineCodeAssembler
    {
        private static readonly HashSet<string> ReservedSlotVariableNames = new HashSet<string>(StringComparer.Ordinal)
        {
            "loaded",
            "state",
            "runEnabled",
            "status",
            "cycleCounter",
            "faultCode",
            "faultInfo",
            "bytecodeSize",
            "source",
            "programType",
            "paramsSummary",
            "inputChannel",
            "outputChannel",
            "runtimeMapOk",
            "start",
            "stop",
            "reset",
            "clearFault",
        };

        private const string PublicSlotVariableVisibility = "PUBLIC";
        private const string PrivateSlotVariableVisibility = "PRIVATE";

        public const byte NopOpcode = 0x01;
        public const byte HaltOpcode = 0x00;
        public const byte PushTrueOpcode = 0x02;
        public const byte PushFalseOpcode = 0x03;
        public const byte DupOpcode = 0x04;
        public const byte DropOpcode = 0x05;
        public const byte SwapOpcode = 0x06;
        public const byte AndOpcode = 0x07;
        public const byte OrOpcode = 0x08;
        public const byte XorOpcode = 0x09;
        public const byte NotOpcode = 0x0A;
        public const byte EqOpcode = 0x0B;
        public const byte NeOpcode = 0x0C;
        public const byte LoadPointBoolOpcode = 0x10;
        public const byte StorePointBoolOpcode = 0x11;
        public const byte LoadPointInt16Opcode = 0x12;
        public const byte StorePointInt16Opcode = 0x13;
        public const byte PushInt16Opcode = 0x14;
        public const byte LoadPointUInt32Opcode = 0x15;
        public const byte StorePointUInt32Opcode = 0x16;
        public const byte LoadPointInt32Opcode = 0x17;
        public const byte StorePointInt32Opcode = 0x18;
        public const byte LoadPointFloat32Opcode = 0x19;
        public const byte StorePointFloat32Opcode = 0x1A;
        public const byte PushUInt32Opcode = 0x1B;
        public const byte PushInt32Opcode = 0x1C;
        public const byte PushFloat32Opcode = 0x1D;
        public const byte IncrementPointIntOpcode = 0x20;
        public const byte DecrementPointIntOpcode = 0x21;
        public const byte AddOpcode = 0x22;
        public const byte SubOpcode = 0x23;
        public const byte LessThanOpcode = 0x24;
        public const byte LessOrEqualOpcode = 0x25;
        public const byte GreaterThanOpcode = 0x26;
        public const byte GreaterOrEqualOpcode = 0x27;
        public const byte MinOpcode = 0x28;
        public const byte MaxOpcode = 0x29;
        public const byte ClampOpcode = 0x2A;
        public const byte SelectOpcode = 0x2B;
        public const byte JumpOpcode = 0x2C;
        public const byte JumpIfZeroOpcode = 0x2D;
        public const byte JumpIfNotZeroOpcode = 0x2E;
        public const byte RisingEdgeTriggerOpcode = 0x2F;
        public const byte FallingEdgeTriggerOpcode = 0x30;
        public const byte TimerOnStartOpcode = 0x31;
        public const byte TimerOnDoneOpcode = 0x32;
        public const byte TimerOnResetOpcode = 0x33;
        public const byte TimerOnElapsedOpcode = 0x34;
        public const byte TimerOnRemainingOpcode = 0x35;
        public const byte TimerOffStartOpcode = 0x36;
        public const byte TimerOffDoneOpcode = 0x37;
        public const byte TimerOffResetOpcode = 0x38;
        public const byte TimerPulseStartOpcode = 0x39;
        public const byte TimerPulseDoneOpcode = 0x3A;
        public const byte TimerPulseResetOpcode = 0x3B;
        public const byte CounterUpCountOpcode = 0x3C;
        public const byte CounterUpDoneOpcode = 0x3D;
        public const byte CounterUpValueOpcode = 0x3E;
        public const byte CounterUpResetOpcode = 0x3F;
        public const byte CounterDownCountOpcode = 0x40;
        public const byte CounterDownDoneOpcode = 0x41;
        public const byte CounterDownValueOpcode = 0x42;
        public const byte CounterDownResetOpcode = 0x43;
        public const byte FloatEqualOpcode = 0x44;
        public const byte FloatNotEqualOpcode = 0x45;
        public const byte FloatLessThanOpcode = 0x46;
        public const byte FloatLessOrEqualOpcode = 0x47;
        public const byte FloatGreaterThanOpcode = 0x48;
        public const byte FloatGreaterOrEqualOpcode = 0x49;
        public const byte SignExtendInt16ToInt32Opcode = 0x4A;
        public const byte TruncateInt32ToInt16Opcode = 0x4B;
        public const byte BoolToUInt32Opcode = 0x4C;
        public const byte BoolToInt32Opcode = 0x4D;
        public const byte UInt32ToBoolOpcode = 0x4E;
        public const byte Int32ToBoolOpcode = 0x4F;

        public static PlcAssemblyResult Assemble(string source, PlcObjectFileOptions options)
        {
            if (source == null)
            {
                throw new ArgumentNullException(nameof(source));
            }
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            var output = new List<byte>();
            var result = new PlcAssemblyResult();
            var symbolIndexByName = new Dictionary<string, ushort>(StringComparer.Ordinal);
            var labels = new Dictionary<string, ushort>(StringComparer.Ordinal);
            using (var reader = new StringReader(source))
            {
                var parsedLines = new List<ParsedSourceLine>();
                string line;
                var lineNumber = 0;
                while ((line = reader.ReadLine()) != null)
                {
                    lineNumber += 1;
                    var sanitized = StripComment(line).Trim();
                    var parsedLine = ParseSourceLine(sanitized, lineNumber);
                    if (parsedLine == null)
                    {
                        continue;
                    }

                    parsedLines.Add(parsedLine);
                }

                var codeOffset = 0;
                foreach (var parsedLine in parsedLines)
                {
                    if (!string.IsNullOrEmpty(parsedLine.Label))
                    {
                        if (labels.ContainsKey(parsedLine.Label))
                        {
                            throw new PlcMachineCodeCompileException($"Label '{parsedLine.Label}' is already declared", parsedLine.LineNumber);
                        }

                        labels[parsedLine.Label] = checked((ushort)codeOffset);
                    }

                    if (parsedLine.Tokens == null || parsedLine.Tokens.Count == 0)
                    {
                        continue;
                    }

                    codeOffset += GetEncodedInstructionSize(parsedLine.Tokens, parsedLine.LineNumber);
                }

                foreach (var parsedLine in parsedLines)
                {
                    var tokens = parsedLine.Tokens;
                    if (tokens == null || tokens.Count == 0)
                    {
                        continue;
                    }

                    var mnemonic = tokens[0].ToUpperInvariant();
                    switch (mnemonic)
                    {
                        case "CONST":
                            ParseConstPointDeclaration(tokens, parsedLine.LineNumber, result, symbolIndexByName);
                            break;

                        case "PARAM":
                            ParseParamPointDeclaration(tokens, parsedLine.LineNumber, result, symbolIndexByName, options);
                            break;

                        case "VAR":
                            ParseVarDeclaration(tokens, parsedLine.LineNumber, result, symbolIndexByName);
                            break;

                        case "NOP":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(NopOpcode);
                            break;

                        case "HALT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(HaltOpcode);
                            break;

                        case "PUSH_TRUE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(PushTrueOpcode);
                            break;

                        case "PUSH_FALSE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(PushFalseOpcode);
                            break;

                        case "DUP":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(DupOpcode);
                            break;

                        case "DROP":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(DropOpcode);
                            break;

                        case "SWAP":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(SwapOpcode);
                            break;

                        case "AND":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(AndOpcode);
                            break;

                        case "OR":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(OrOpcode);
                            break;

                        case "XOR":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(XorOpcode);
                            break;

                        case "NOT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(NotOpcode);
                            break;

                        case "EQ":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(EqOpcode);
                            break;

                        case "NE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(NeOpcode);
                            break;

                        case "FEQ":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatEqualOpcode);
                            break;

                        case "FNE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatNotEqualOpcode);
                            break;

                        case "FLT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatLessThanOpcode);
                            break;

                        case "FLE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatLessOrEqualOpcode);
                            break;

                        case "FGT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatGreaterThanOpcode);
                            break;

                        case "FGE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(FloatGreaterOrEqualOpcode);
                            break;

                        case "PUSH_I16":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(PushInt16Opcode);
                            WriteUInt16(output, ParseInt16Literal(tokens[1], parsedLine.LineNumber));
                            break;

                        case "PUSH_U32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(PushUInt32Opcode);
                            WriteUInt32(output, ParseUInt32Literal(tokens[1], parsedLine.LineNumber, "imm32"));
                            break;

                        case "PUSH_I32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(PushInt32Opcode);
                            WriteUInt32(output, ParseInt32Literal(tokens[1], parsedLine.LineNumber));
                            break;

                        case "PUSH_F32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(PushFloat32Opcode);
                            WriteUInt32(output, ParseFloat32Literal(tokens[1], parsedLine.LineNumber));
                            break;

                        case "LOAD_BOOL":
                        case "LOAD_POINT_BOOL":
                        case "LB":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(LoadPointBoolOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "STORE_BOOL":
                        case "STORE_POINT_BOOL":
                        case "SB":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(StorePointBoolOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Write,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "LOAD_I16":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(LoadPointInt16Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "LOAD_U32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(LoadPointUInt32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Uint32,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "LOAD_I32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(LoadPointInt32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int32,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "LOAD_F32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(LoadPointFloat32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Float,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "STORE_I16":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(StorePointInt16Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.Write,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "STORE_U32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(StorePointUInt32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Uint32,
                                                 PlcRuntimeLinkAccess.Write,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "STORE_I32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(StorePointInt32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int32,
                                                 PlcRuntimeLinkAccess.Write,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "STORE_F32":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(StorePointFloat32Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Float,
                                                 PlcRuntimeLinkAccess.Write,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "ADD":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(AddOpcode);
                            break;

                        case "SUB":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(SubOpcode);
                            break;

                        case "LT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(LessThanOpcode);
                            break;

                        case "LE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(LessOrEqualOpcode);
                            break;

                        case "GT":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(GreaterThanOpcode);
                            break;

                        case "GE":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(GreaterOrEqualOpcode);
                            break;

                        case "MIN":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(MinOpcode);
                            break;

                        case "MAX":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(MaxOpcode);
                            break;

                        case "CLAMP":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(ClampOpcode);
                            break;

                        case "SEL":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(SelectOpcode);
                            break;

                        case "SX_I16_TO_I32":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(SignExtendInt16ToInt32Opcode);
                            break;

                        case "TRUNC_I32_TO_I16":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(TruncateInt32ToInt16Opcode);
                            break;

                        case "BOOL_TO_U32":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(BoolToUInt32Opcode);
                            break;

                        case "BOOL_TO_I32":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(BoolToInt32Opcode);
                            break;

                        case "U32_TO_BOOL":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(UInt32ToBoolOpcode);
                            break;

                        case "I32_TO_BOOL":
                            RequireOperandCount(tokens, 1, parsedLine.LineNumber);
                            output.Add(Int32ToBoolOpcode);
                            break;

                        case "JMP":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(JumpOpcode);
                            WriteUInt16(output, ResolveBranchOffset(tokens[1], parsedLine.LineNumber, output.Count, labels));
                            break;

                        case "JZ":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(JumpIfZeroOpcode);
                            WriteUInt16(output, ResolveBranchOffset(tokens[1], parsedLine.LineNumber, output.Count, labels));
                            break;

                        case "JNZ":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(JumpIfNotZeroOpcode);
                            WriteUInt16(output, ResolveBranchOffset(tokens[1], parsedLine.LineNumber, output.Count, labels));
                            break;

                        case "R_TRIG":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(RisingEdgeTriggerOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateIndexU16Le);
                            break;

                        case "F_TRIG":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(FallingEdgeTriggerOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Read,
                                                 PlcObjectRelocationKind.PointStateIndexU16Le);
                            break;

                        case "TON_START":
                            RequireOperandCount(tokens, 3, parsedLine.LineNumber);
                            output.Add(TimerOnStartOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            WriteUInt32(output, ParseUInt32Literal(tokens[2], parsedLine.LineNumber, "preset_ms32"));
                            break;

                        case "TON_DONE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOnDoneOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TON_RESET":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOnResetOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TON_ELAPSED":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOnElapsedOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TON_REMAINING":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOnRemainingOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TOF_START":
                            RequireOperandCount(tokens, 3, parsedLine.LineNumber);
                            output.Add(TimerOffStartOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            WriteUInt32(output, ParseUInt32Literal(tokens[2], parsedLine.LineNumber, "preset_ms32"));
                            break;

                        case "TOF_DONE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOffDoneOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TOF_RESET":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerOffResetOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TP_START":
                            RequireOperandCount(tokens, 3, parsedLine.LineNumber);
                            output.Add(TimerPulseStartOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            WriteUInt32(output, ParseUInt32Literal(tokens[2], parsedLine.LineNumber, "preset_ms32"));
                            break;

                        case "TP_DONE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerPulseDoneOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "TP_RESET":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(TimerPulseResetOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "timer index"));
                            break;

                        case "CTU_COUNT":
                            RequireOperandCount(tokens, 3, parsedLine.LineNumber);
                            output.Add(CounterUpCountOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            WriteUInt16(output, ParseInt16Literal(tokens[2], parsedLine.LineNumber));
                            break;

                        case "CTU_DONE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterUpDoneOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "CTU_VALUE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterUpValueOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "CTU_RESET":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterUpResetOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "CTD_COUNT":
                            RequireOperandCount(tokens, 3, parsedLine.LineNumber);
                            output.Add(CounterDownCountOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            WriteUInt16(output, ParseInt16Literal(tokens[2], parsedLine.LineNumber));
                            break;

                        case "CTD_DONE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterDownDoneOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "CTD_VALUE":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterDownValueOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "CTD_RESET":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(CounterDownResetOpcode);
                            WriteUInt16(output, ParseUInt16Literal(tokens[1], parsedLine.LineNumber, "counter index"));
                            break;

                        case "INC_INT":
                        case "INC":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(IncrementPointIntOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.ReadWrite,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "DEC_INT":
                        case "DEC":
                            RequireOperandCount(tokens, 2, parsedLine.LineNumber);
                            output.Add(DecrementPointIntOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 parsedLine.LineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.ReadWrite,
                                                 PlcObjectRelocationKind.PointStateValueOffsetU32Le);
                            break;

                        case "DB":
                            if (tokens.Count < 2)
                            {
                                throw new PlcMachineCodeCompileException("DB requires at least one byte operand", parsedLine.LineNumber);
                            }

                            for (var index = 1; index < tokens.Count; index += 1)
                            {
                                output.Add(ParseByte(tokens[index], parsedLine.LineNumber));
                            }
                            break;

                        default:
                            throw new PlcMachineCodeCompileException($"Unsupported instruction '{tokens[0]}'", parsedLine.LineNumber);
                    }
                }
            }

            result.CodeBytes = output.ToArray();
            return result;
        }

        private sealed class ParsedSourceLine
        {
            public int LineNumber { get; set; }

            public string Label { get; set; } = string.Empty;

            public List<string> Tokens { get; set; } = new List<string>();
        }

        private static ParsedSourceLine ParseSourceLine(string line, int lineNumber)
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                return null;
            }

            var workingLine = line;
            var label = string.Empty;
            var colonIndex = workingLine.IndexOf(':');
            if (colonIndex >= 0)
            {
                label = workingLine.Substring(0, colonIndex).Trim();
                if (label.Length == 0)
                {
                    throw new PlcMachineCodeCompileException("Label name is required before ':'", lineNumber);
                }

                ValidateLabelName(label, lineNumber);
                workingLine = workingLine.Substring(colonIndex + 1).Trim();
            }

            var tokens = Tokenize(workingLine);
            if (label.Length == 0 && tokens.Count == 0)
            {
                return null;
            }

            return new ParsedSourceLine
            {
                LineNumber = lineNumber,
                Label = label,
                Tokens = tokens,
            };
        }

        private static void ValidateLabelName(string label, int lineNumber)
        {
            foreach (var character in label)
            {
                if (!(char.IsLetterOrDigit(character) || character == '_'))
                {
                    throw new PlcMachineCodeCompileException($"Invalid label '{label}'", lineNumber);
                }
            }
        }

        private static int GetEncodedInstructionSize(List<string> tokens, int lineNumber)
        {
            if (tokens == null || tokens.Count == 0)
            {
                return 0;
            }

            var mnemonic = tokens[0].ToUpperInvariant();
            switch (mnemonic)
            {
                case "CONST":
                case "PARAM":
                case "VAR":
                    return 0;
                case "PUSH_I16":
                case "JMP":
                case "JZ":
                case "JNZ":
                case "R_TRIG":
                case "F_TRIG":
                case "TON_DONE":
                case "TON_RESET":
                case "TON_ELAPSED":
                case "TON_REMAINING":
                case "TOF_DONE":
                case "TOF_RESET":
                case "TP_DONE":
                case "TP_RESET":
                case "CTU_DONE":
                case "CTU_VALUE":
                case "CTU_RESET":
                case "CTD_DONE":
                case "CTD_VALUE":
                case "CTD_RESET":
                    return 3;
                case "LOAD_U32":
                case "STORE_U32":
                case "LOAD_I32":
                case "STORE_I32":
                case "LOAD_F32":
                case "STORE_F32":
                case "LOAD_BOOL":
                case "LOAD_POINT_BOOL":
                case "LB":
                case "STORE_BOOL":
                case "STORE_POINT_BOOL":
                case "SB":
                case "LOAD_I16":
                case "STORE_I16":
                case "INC_INT":
                case "INC":
                case "DEC_INT":
                case "DEC":
                    return 5;
                case "PUSH_U32":
                case "PUSH_I32":
                case "PUSH_F32":
                    return 5;
                case "CTU_COUNT":
                case "CTD_COUNT":
                    return 5;
                case "TON_START":
                case "TOF_START":
                case "TP_START":
                    return 7;
                case "DB":
                    return tokens.Count - 1;
                default:
                    if (tokens.Count == 1)
                    {
                        return 1;
                    }

                    throw new PlcMachineCodeCompileException($"Unsupported instruction '{tokens[0]}'", lineNumber);
            }
        }

        private static ushort ResolveBranchOffset(string operand,
                                                  int lineNumber,
                                                  int currentOutputOffset,
                                                  IDictionary<string, ushort> labels)
        {
            var branchBase = currentOutputOffset + 2;
            if (TryParseSignedBranchOffset(operand, out var signedOffset))
            {
                return unchecked((ushort)(short)signedOffset);
            }

            if (!labels.TryGetValue(operand, out var targetOffset))
            {
                throw new PlcMachineCodeCompileException($"Unknown branch label '{operand}'", lineNumber);
            }

            signedOffset = targetOffset - branchBase;
            if (signedOffset < short.MinValue || signedOffset > short.MaxValue)
            {
                throw new PlcMachineCodeCompileException($"Branch target '{operand}' is out of rel16 range", lineNumber);
            }

            return unchecked((ushort)(short)signedOffset);
        }

        private static bool TryParseSignedBranchOffset(string token, out int value)
        {
            value = 0;
            try
            {
                value = (short)ParseInt16Literal(token, 0);
                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }

        private static string StripComment(string line)
        {
            var semicolonIndex = line.IndexOf(';');
            var hashIndex = line.IndexOf('#');
            var commentIndex = -1;

            if (semicolonIndex >= 0 && hashIndex >= 0)
            {
                commentIndex = Math.Min(semicolonIndex, hashIndex);
            }
            else if (semicolonIndex >= 0)
            {
                commentIndex = semicolonIndex;
            }
            else if (hashIndex >= 0)
            {
                commentIndex = hashIndex;
            }

            return commentIndex >= 0 ? line.Substring(0, commentIndex) : line;
        }

        private static List<string> Tokenize(string line)
        {
            var tokens = new List<string>();
            foreach (var rawToken in line.Split(new[] { ' ', '\t', ',' }, StringSplitOptions.RemoveEmptyEntries))
            {
                tokens.Add(rawToken.Trim());
            }

            return tokens;
        }

        private static void RequireOperandCount(List<string> tokens, int expectedCount, int lineNumber)
        {
            if (tokens.Count != expectedCount)
            {
                throw new PlcMachineCodeCompileException($"Expected {expectedCount - 1} operand(s) for {tokens[0]}", lineNumber);
            }
        }

        private static void ParseConstPointDeclaration(List<string> tokens,
                                                       int lineNumber,
                                                       PlcAssemblyResult result,
                                                       IDictionary<string, ushort> symbolIndexByName)
        {
            if (tokens.Count != 4 || !string.Equals(tokens[1], "POINT_ID", StringComparison.OrdinalIgnoreCase))
            {
                throw new PlcMachineCodeCompileException("CONST syntax is 'CONST POINT_ID <symbol>, <deviceId.feature.pointId>'", lineNumber);
            }

            AddSymbol(result,
                      symbolIndexByName,
                      lineNumber,
                      tokens[2],
                      PlcObjectSymbolKind.ConstPointId,
                      tokens[3]);
        }

        private static void ParseParamPointDeclaration(List<string> tokens,
                                                       int lineNumber,
                                                       PlcAssemblyResult result,
                                                       IDictionary<string, ushort> symbolIndexByName,
                                                       PlcObjectFileOptions options)
        {
            if (tokens.Count != 3 || !string.Equals(tokens[1], "POINT_ID", StringComparison.OrdinalIgnoreCase))
            {
                throw new PlcMachineCodeCompileException("PARAM syntax is 'PARAM POINT_ID <symbol>'", lineNumber);
            }

            if (options.PointBindings == null || !options.PointBindings.TryGetValue(tokens[2], out var pointPath))
            {
                throw new PlcMachineCodeCompileException($"Missing point binding for PARAM '{tokens[2]}'", lineNumber);
            }

            AddSymbol(result,
                      symbolIndexByName,
                      lineNumber,
                      tokens[2],
                      PlcObjectSymbolKind.ParamPointId,
                      pointPath,
                      byte.MaxValue,
                      PlcRuntimeLinkAccess.Read);
        }

        private static void ParseVarDeclaration(List<string> tokens,
                                                int lineNumber,
                                                PlcAssemblyResult result,
                                                IDictionary<string, ushort> symbolIndexByName)
        {
            if (tokens.Count != 3 && tokens.Count != 4)
            {
                throw new PlcMachineCodeCompileException("VAR syntax is 'VAR <type> <name>' or 'VAR PUBLIC|PRIVATE <type> <name>'", lineNumber);
            }

            var visibilityTokenIndex = tokens.Count == 4 ? 1 : -1;
            var typeTokenIndex = tokens.Count == 4 ? 2 : 1;
            var nameTokenIndex = tokens.Count == 4 ? 3 : 2;
            var flags = PlcAssemblySymbolFlags.None;

            if (visibilityTokenIndex >= 0)
            {
                var visibility = tokens[visibilityTokenIndex].ToUpperInvariant();
                if (visibility == PrivateSlotVariableVisibility)
                {
                    flags = PlcAssemblySymbolFlags.SlotVarPrivate;
                }
                else if (visibility != PublicSlotVariableVisibility)
                {
                    throw new PlcMachineCodeCompileException("VAR visibility must be PUBLIC or PRIVATE", lineNumber);
                }
            }

            var valueType = ParseValueType(tokens[typeTokenIndex], lineNumber);
            if (ReservedSlotVariableNames.Contains(tokens[nameTokenIndex]))
            {
                throw new PlcMachineCodeCompileException(
                    $"VAR name '{tokens[nameTokenIndex]}' is reserved by plc.slot runtime points",
                    lineNumber);
            }

            AddSymbol(result,
                      symbolIndexByName,
                      lineNumber,
                      tokens[nameTokenIndex],
                      PlcObjectSymbolKind.SlotVar,
                      string.Empty,
                      (byte)valueType,
                      PlcRuntimeLinkAccess.ReadWrite,
                      flags);
        }

        private static void AddSymbol(PlcAssemblyResult result,
                                      IDictionary<string, ushort> symbolIndexByName,
                                      int lineNumber,
                                      string symbolName,
                                      PlcObjectSymbolKind kind,
                                      string pointPath,
                                      byte expectedType = byte.MaxValue,
                                      PlcRuntimeLinkAccess access = PlcRuntimeLinkAccess.Read,
                                      PlcAssemblySymbolFlags flags = PlcAssemblySymbolFlags.None)
        {
            if (string.IsNullOrWhiteSpace(symbolName))
            {
                throw new PlcMachineCodeCompileException("Symbol name is required", lineNumber);
            }
            if (symbolIndexByName.ContainsKey(symbolName))
            {
                throw new PlcMachineCodeCompileException($"Symbol '{symbolName}' is already declared", lineNumber);
            }

            if (result.Symbols.Count >= ushort.MaxValue)
            {
                throw new PlcMachineCodeCompileException("Too many symbols", lineNumber);
            }

            symbolIndexByName[symbolName] = (ushort)result.Symbols.Count;
            result.Symbols.Add(new PlcAssemblySymbol
            {
                Name = symbolName,
                Kind = kind,
                PointPath = pointPath,
                ExpectedType = expectedType,
                Access = access,
                Flags = flags,
            });
        }

        private static void WriteRelocatedSymbol(List<byte> output,
                                                 string symbolName,
                                                 int lineNumber,
                                                 PlcAssemblyResult result,
                                                 IDictionary<string, ushort> symbolIndexByName,
                                                 PlcValueType expectedType,
                                                 PlcRuntimeLinkAccess access,
                                                 PlcObjectRelocationKind relocationKind)
        {
            if (!symbolIndexByName.TryGetValue(symbolName, out var symbolIndex))
            {
                throw new PlcMachineCodeCompileException($"Unknown symbol '{symbolName}'", lineNumber);
            }

            var symbol = result.Symbols[symbolIndex];
            ApplySymbolUsageType(symbol, expectedType, lineNumber);
            symbol.Access = MergeAccess(symbol.Access, access);

            result.Relocations.Add(new PlcAssemblyRelocation
            {
                CodeOffset = (uint)output.Count,
                SymbolIndex = symbolIndex,
                RelocationKind = (byte)relocationKind,
            });
            if (relocationKind == PlcObjectRelocationKind.PointStateIndexU16Le)
            {
                WriteUInt16(output, 0);
            }
            else
            {
                WriteUInt32(output, 0u);
            }
        }

        private static void ApplySymbolUsageType(PlcAssemblySymbol symbol, PlcValueType expectedType, int lineNumber)
        {
            if (symbol == null)
            {
                throw new ArgumentNullException(nameof(symbol));
            }

            if (symbol.ExpectedType == byte.MaxValue)
            {
                symbol.ExpectedType = (byte)expectedType;
                return;
            }

            if (symbol.ExpectedType != (byte)expectedType)
            {
                throw new PlcMachineCodeCompileException(
                    $"Symbol '{symbol.Name}' is declared or used as {FormatValueType(symbol.ExpectedType)} and cannot be used as {FormatValueType((byte)expectedType)}",
                    lineNumber);
            }
        }

        private static PlcRuntimeLinkAccess MergeAccess(PlcRuntimeLinkAccess current, PlcRuntimeLinkAccess next)
        {
            if (current == PlcRuntimeLinkAccess.ReadWrite || next == PlcRuntimeLinkAccess.ReadWrite)
            {
                return PlcRuntimeLinkAccess.ReadWrite;
            }

            if (current == next)
            {
                return current;
            }

            return PlcRuntimeLinkAccess.ReadWrite;
        }

        private static PlcValueType ParseValueType(string token, int lineNumber)
        {
            if (string.IsNullOrWhiteSpace(token))
            {
                throw new PlcMachineCodeCompileException("Value type is required", lineNumber);
            }

            switch (token.Trim().ToUpperInvariant())
            {
                case "BOOL":
                    return PlcValueType.Bool;
                case "UINT16":
                case "WORD":
                    return PlcValueType.Uint16;
                case "INT":
                case "INT16":
                    return PlcValueType.Int16;
                case "UINT32":
                case "DWORD":
                    return PlcValueType.Uint32;
                case "DINT":
                case "INT32":
                    return PlcValueType.Int32;
                case "FLOAT":
                case "REAL":
                    return PlcValueType.Float;
                case "ENUM":
                    return PlcValueType.Enum;
                case "STRING":
                    return PlcValueType.String;
                default:
                    throw new PlcMachineCodeCompileException($"Unsupported value type '{token}'", lineNumber);
            }
        }

        private static string FormatValueType(byte rawType)
        {
            switch ((PlcValueType)rawType)
            {
                case PlcValueType.Bool:
                    return "BOOL";
                case PlcValueType.Uint16:
                    return "UINT16";
                case PlcValueType.Int16:
                    return "INT";
                case PlcValueType.Uint32:
                    return "UINT32";
                case PlcValueType.Int32:
                    return "DINT";
                case PlcValueType.Float:
                    return "FLOAT";
                case PlcValueType.Enum:
                    return "ENUM";
                case PlcValueType.String:
                    return "STRING";
                default:
                    return $"TYPE_{rawType}";
            }
        }

        private static byte ParseByte(string text, int lineNumber)
        {
            try
            {
                uint value;
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    value = Convert.ToUInt32(text.Substring(2), 16);
                }
                else
                {
                    value = Convert.ToUInt32(text);
                }

                if (value > byte.MaxValue)
                {
                    throw new PlcMachineCodeCompileException($"Value '{text}' is outside byte range", lineNumber);
                }

                return (byte)value;
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid byte literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid byte literal '{text}'", lineNumber);
            }
        }

        private static ushort ParseInt16Literal(string text, int lineNumber)
        {
            try
            {
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    var hexValue = Convert.ToUInt32(text.Substring(2), 16);
                    if (hexValue > ushort.MaxValue)
                    {
                        throw new PlcMachineCodeCompileException($"Value '{text}' is outside 16-bit range", lineNumber);
                    }

                    return (ushort)hexValue;
                }

                var signedValue = Convert.ToInt32(text);
                if (signedValue < short.MinValue || signedValue > short.MaxValue)
                {
                    throw new PlcMachineCodeCompileException($"Value '{text}' is outside INT16 range", lineNumber);
                }

                return unchecked((ushort)(short)signedValue);
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid INT16 literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid INT16 literal '{text}'", lineNumber);
            }
        }

        private static ushort ParseUInt16Literal(string text, int lineNumber, string description)
        {
            try
            {
                uint value;
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    value = Convert.ToUInt32(text.Substring(2), 16);
                }
                else
                {
                    value = Convert.ToUInt32(text);
                }

                if (value > ushort.MaxValue)
                {
                    throw new PlcMachineCodeCompileException($"Value '{text}' is outside {description} UINT16 range", lineNumber);
                }

                return (ushort)value;
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid {description} literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid {description} literal '{text}'", lineNumber);
            }
        }

        private static uint ParseUInt32Literal(string text, int lineNumber, string description)
        {
            try
            {
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    return Convert.ToUInt32(text.Substring(2), 16);
                }

                return Convert.ToUInt32(text);
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid {description} literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid {description} literal '{text}'", lineNumber);
            }
        }

        private static uint ParseInt32Literal(string text, int lineNumber)
        {
            try
            {
                if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    return Convert.ToUInt32(text.Substring(2), 16);
                }

                var signedValue = Convert.ToInt32(text, System.Globalization.CultureInfo.InvariantCulture);
                return unchecked((uint)signedValue);
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid INT32 literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid INT32 literal '{text}'", lineNumber);
            }
        }

        private static uint ParseFloat32Literal(string text, int lineNumber)
        {
            try
            {
                var value = float.Parse(text, System.Globalization.CultureInfo.InvariantCulture);
                return BitConverter.ToUInt32(BitConverter.GetBytes(value), 0);
            }
            catch (FormatException)
            {
                throw new PlcMachineCodeCompileException($"Invalid FLOAT literal '{text}'", lineNumber);
            }
            catch (OverflowException)
            {
                throw new PlcMachineCodeCompileException($"Invalid FLOAT literal '{text}'", lineNumber);
            }
        }

        private static void WriteUInt16(List<byte> output, ushort value)
        {
            output.Add((byte)(value & 0xFFu));
            output.Add((byte)((value >> 8) & 0xFFu));
        }

        private static void WriteUInt32(List<byte> output, uint value)
        {
            output.Add((byte)(value & 0xFFu));
            output.Add((byte)((value >> 8) & 0xFFu));
            output.Add((byte)((value >> 16) & 0xFFu));
            output.Add((byte)((value >> 24) & 0xFFu));
        }
    }
}