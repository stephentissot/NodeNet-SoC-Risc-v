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
        public const byte IncrementPointIntOpcode = 0x20;
        public const byte DecrementPointIntOpcode = 0x21;
        public const byte AddOpcode = 0x22;
        public const byte SubOpcode = 0x23;

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
            using (var reader = new StringReader(source))
            {
                string line;
                var lineNumber = 0;
                while ((line = reader.ReadLine()) != null)
                {
                    lineNumber += 1;
                    var sanitized = StripComment(line).Trim();
                    if (sanitized.Length == 0)
                    {
                        continue;
                    }

                    var tokens = Tokenize(sanitized);
                    if (tokens.Count == 0)
                    {
                        continue;
                    }

                    var mnemonic = tokens[0].ToUpperInvariant();
                    switch (mnemonic)
                    {
                        case "CONST":
                            ParseConstPointDeclaration(tokens, lineNumber, result, symbolIndexByName);
                            break;

                        case "PARAM":
                            ParseParamPointDeclaration(tokens, lineNumber, result, symbolIndexByName, options);
                            break;

                        case "VAR":
                            ParseVarDeclaration(tokens, lineNumber, result, symbolIndexByName);
                            break;

                        case "NOP":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(NopOpcode);
                            break;

                        case "HALT":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(HaltOpcode);
                            break;

                        case "PUSH_TRUE":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(PushTrueOpcode);
                            break;

                        case "PUSH_FALSE":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(PushFalseOpcode);
                            break;

                        case "DUP":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(DupOpcode);
                            break;

                        case "DROP":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(DropOpcode);
                            break;

                        case "SWAP":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(SwapOpcode);
                            break;

                        case "AND":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(AndOpcode);
                            break;

                        case "OR":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(OrOpcode);
                            break;

                        case "XOR":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(XorOpcode);
                            break;

                        case "NOT":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(NotOpcode);
                            break;

                        case "EQ":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(EqOpcode);
                            break;

                        case "NE":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(NeOpcode);
                            break;

                        case "PUSH_I16":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(PushInt16Opcode);
                            WriteUInt16(output, ParseInt16Literal(tokens[1], lineNumber));
                            break;

                        case "LOAD_BOOL":
                        case "LOAD_POINT_BOOL":
                        case "LB":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(LoadPointBoolOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Read);
                            break;

                        case "STORE_BOOL":
                        case "STORE_POINT_BOOL":
                        case "SB":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(StorePointBoolOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Bool,
                                                 PlcRuntimeLinkAccess.Write);
                            break;

                        case "LOAD_I16":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(LoadPointInt16Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.Read);
                            break;

                        case "STORE_I16":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(StorePointInt16Opcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.Write);
                            break;

                        case "ADD":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(AddOpcode);
                            break;

                        case "SUB":
                            RequireOperandCount(tokens, 1, lineNumber);
                            output.Add(SubOpcode);
                            break;

                        case "INC_INT":
                        case "INC":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(IncrementPointIntOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.ReadWrite);
                            break;

                        case "DEC_INT":
                        case "DEC":
                            RequireOperandCount(tokens, 2, lineNumber);
                            output.Add(DecrementPointIntOpcode);
                            WriteRelocatedSymbol(output,
                                                 tokens[1],
                                                 lineNumber,
                                                 result,
                                                 symbolIndexByName,
                                                 PlcValueType.Int16,
                                                 PlcRuntimeLinkAccess.ReadWrite);
                            break;

                        case "DB":
                            if (tokens.Count < 2)
                            {
                                throw new PlcMachineCodeCompileException("DB requires at least one byte operand", lineNumber);
                            }

                            for (var index = 1; index < tokens.Count; index += 1)
                            {
                                output.Add(ParseByte(tokens[index], lineNumber));
                            }
                            break;

                        default:
                            throw new PlcMachineCodeCompileException($"Unsupported instruction '{tokens[0]}'", lineNumber);
                    }
                }
            }

            result.CodeBytes = output.ToArray();
            return result;
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
                                                 PlcRuntimeLinkAccess access)
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
                RelocationKind = 0,
            });
            WriteUInt16(output, 0);
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

        private static void WriteUInt16(List<byte> output, ushort value)
        {
            output.Add((byte)(value & 0xFFu));
            output.Add((byte)((value >> 8) & 0xFFu));
        }
    }
}