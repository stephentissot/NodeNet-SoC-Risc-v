using System;
using System.Collections.Generic;
using System.Text;

namespace BigSisterNodeNet.Plc
{
    public sealed class PlcObjectFileParseException : Exception
    {
        public PlcObjectFileParseException(string message)
            : base(message)
        {
        }
    }

    public sealed class PlcDisassemblyResult
    {
        public string Source { get; set; } = string.Empty;
        public byte[] CodeBytes { get; set; } = Array.Empty<byte>();
        public IList<PlcAssemblySymbol> Symbols { get; } = new List<PlcAssemblySymbol>();
        public IList<PlcAssemblyRelocation> Relocations { get; } = new List<PlcAssemblyRelocation>();
    }

    public static class PlcMachineCodeDisassembler
    {
        public static string DisassembleBytecode(byte[] codeBytes)
        {
            return DisassembleBytecode(codeBytes, null);
        }

        public static string DisassembleBytecode(byte[] codeBytes, Func<uint, string> operandFormatter)
        {
            if (codeBytes == null)
            {
                throw new ArgumentNullException(nameof(codeBytes));
            }

            return BuildInstructionListing(codeBytes, null, operandFormatter);
        }

        public static PlcDisassemblyResult DisassembleObjectFile(byte[] objectFileBytes)
        {
            if (objectFileBytes == null)
            {
                throw new ArgumentNullException(nameof(objectFileBytes));
            }

            if (objectFileBytes.Length < PlcObjectFileBuilder.ObjectFileHeaderSize)
            {
                throw new PlcObjectFileParseException("Object file is too small to contain a valid header.");
            }

            var magic = ReadUInt32(objectFileBytes, 0);
            if (magic != PlcObjectFileBuilder.ObjectFileMagic)
            {
                throw new PlcObjectFileParseException("Object file magic does not match objectFileV1.");
            }

            var version = ReadUInt16(objectFileBytes, 4);
            if (version != PlcObjectFileBuilder.ObjectFileVersion)
            {
                throw new PlcObjectFileParseException($"Unsupported object file version {version}.");
            }

            var totalSize = ReadUInt32(objectFileBytes, 12);
            if (totalSize > objectFileBytes.Length)
            {
                throw new PlcObjectFileParseException("Object file total size exceeds the provided payload length.");
            }

            var codeSize = ReadUInt32(objectFileBytes, 16);
            var symbolCount = ReadUInt16(objectFileBytes, 24);
            var relocationCount = ReadUInt16(objectFileBytes, 26);
            var symbolTableOffset = ReadUInt32(objectFileBytes, 28);
            var relocationTableOffset = ReadUInt32(objectFileBytes, 32);

            EnsureRange(symbolTableOffset, (uint)(symbolCount * PlcObjectFileBuilder.SymbolRecordSize), totalSize, "symbol table");
            EnsureRange(relocationTableOffset, (uint)(relocationCount * PlcObjectFileBuilder.RelocationRecordSize), totalSize, "relocation table");
            EnsureRange((uint)PlcObjectFileBuilder.ObjectFileHeaderSize, codeSize, totalSize, "code section");

            var result = new PlcDisassemblyResult();
            var codeBytes = new byte[codeSize];
            Buffer.BlockCopy(objectFileBytes, PlcObjectFileBuilder.ObjectFileHeaderSize, codeBytes, 0, (int)codeSize);
            result.CodeBytes = codeBytes;

            for (var symbolIndex = 0; symbolIndex < symbolCount; symbolIndex += 1)
            {
                var baseOffset = checked((int)symbolTableOffset + (symbolIndex * PlcObjectFileBuilder.SymbolRecordSize));
                var symbol = new PlcAssemblySymbol
                {
                    Name = ReadFixedAscii(objectFileBytes, baseOffset, PlcObjectFileBuilder.SymbolNameSize),
                    Kind = (PlcObjectSymbolKind)objectFileBytes[baseOffset + PlcObjectFileBuilder.SymbolNameSize],
                    Flags = (PlcAssemblySymbolFlags)objectFileBytes[baseOffset + PlcObjectFileBuilder.SymbolNameSize + 1],
                    PointPath = BuildPointPath(
                        ReadFixedAscii(objectFileBytes, baseOffset + 18, PlcObjectFileBuilder.DeviceIdSize),
                        ReadFixedAscii(objectFileBytes, baseOffset + 34, PlcObjectFileBuilder.FeatureSize),
                        ReadFixedAscii(objectFileBytes, baseOffset + 66, PlcObjectFileBuilder.PointIdSize)),
                    ExpectedType = objectFileBytes[baseOffset + 98],
                    Access = (PlcRuntimeLinkAccess)objectFileBytes[baseOffset + 99],
                };
                result.Symbols.Add(symbol);
            }

            for (var relocationIndex = 0; relocationIndex < relocationCount; relocationIndex += 1)
            {
                var baseOffset = checked((int)relocationTableOffset + (relocationIndex * PlcObjectFileBuilder.RelocationRecordSize));
                result.Relocations.Add(new PlcAssemblyRelocation
                {
                    CodeOffset = ReadUInt32(objectFileBytes, baseOffset),
                    SymbolIndex = ReadUInt16(objectFileBytes, baseOffset + 4),
                    RelocationKind = objectFileBytes[baseOffset + 6],
                });
            }

            result.Source = BuildObjectFileSource(result);
            return result;
        }

        private static string BuildObjectFileSource(PlcDisassemblyResult result)
        {
            var builder = new StringBuilder();
            foreach (var symbol in result.Symbols)
            {
                if (symbol.Kind == PlcObjectSymbolKind.SlotVar)
                {
                    builder.Append("VAR ")
                           .Append(FormatSlotVariableVisibility(symbol.Flags))
                           .Append(FormatValueType(symbol.ExpectedType))
                           .Append(' ')
                           .Append(symbol.Name)
                           .AppendLine();
                }
                else if (symbol.Kind == PlcObjectSymbolKind.ParamPointId)
                {
                    builder.Append("PARAM")
                           .Append(" POINT_ID ")
                           .Append(symbol.Name)
                           .AppendLine();
                }
                else
                {
                    builder.Append("CONST")
                           .Append(" POINT_ID ")
                           .Append(symbol.Name)
                           .Append(", ")
                           .Append(symbol.PointPath)
                           .AppendLine();
                }
            }

            if (result.Symbols.Count > 0)
            {
                builder.AppendLine();
            }

            builder.Append(BuildInstructionListing(result.CodeBytes,
                                                   result.Relocations,
                                                   symbolIndex => ResolveObjectSymbolName(result.Symbols, checked((ushort)symbolIndex))));
            return builder.ToString().TrimEnd();
        }

        private static string BuildInstructionListing(byte[] codeBytes,
                                                      IList<PlcAssemblyRelocation> relocations,
                                                      Func<uint, string> operandFormatter)
        {
            var builder = new StringBuilder();
            var relocationByOffset = new Dictionary<uint, PlcAssemblyRelocation>();
            if (relocations != null)
            {
                foreach (var relocation in relocations)
                {
                    relocationByOffset[relocation.CodeOffset] = relocation;
                }
            }

            var pc = 0;
            while (pc < codeBytes.Length)
            {
                var opcode = codeBytes[pc];
                switch (opcode)
                {
                    case PlcMachineCodeAssembler.NopOpcode:
                        builder.AppendLine("NOP");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.HaltOpcode:
                        builder.AppendLine("HALT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.PushTrueOpcode:
                        builder.AppendLine("PUSH_TRUE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.PushFalseOpcode:
                        builder.AppendLine("PUSH_FALSE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.DupOpcode:
                        builder.AppendLine("DUP");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.DropOpcode:
                        builder.AppendLine("DROP");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.SwapOpcode:
                        builder.AppendLine("SWAP");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.AndOpcode:
                        builder.AppendLine("AND");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.OrOpcode:
                        builder.AppendLine("OR");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.XorOpcode:
                        builder.AppendLine("XOR");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.NotOpcode:
                        builder.AppendLine("NOT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.EqOpcode:
                        builder.AppendLine("EQ");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.NeOpcode:
                        builder.AppendLine("NE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatEqualOpcode:
                        builder.AppendLine("FEQ");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatNotEqualOpcode:
                        builder.AppendLine("FNE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatLessThanOpcode:
                        builder.AppendLine("FLT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatLessOrEqualOpcode:
                        builder.AppendLine("FLE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatGreaterThanOpcode:
                        builder.AppendLine("FGT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatGreaterOrEqualOpcode:
                        builder.AppendLine("FGE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatAddOpcode:
                        builder.AppendLine("FADD");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatSubOpcode:
                        builder.AppendLine("FSUB");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatMulOpcode:
                        builder.AppendLine("FMUL");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.FloatDivOpcode:
                        builder.AppendLine("FDIV");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.AddOpcode:
                        builder.AppendLine("ADD");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.SubOpcode:
                        builder.AppendLine("SUB");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.LessThanOpcode:
                        builder.AppendLine("LT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.LessOrEqualOpcode:
                        builder.AppendLine("LE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.GreaterThanOpcode:
                        builder.AppendLine("GT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.GreaterOrEqualOpcode:
                        builder.AppendLine("GE");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.MinOpcode:
                        builder.AppendLine("MIN");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.MaxOpcode:
                        builder.AppendLine("MAX");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.ClampOpcode:
                        builder.AppendLine("CLAMP");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.SelectOpcode:
                        builder.AppendLine("SEL");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.SignExtendInt16ToInt32Opcode:
                        builder.AppendLine("SX_I16_TO_I32");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.TruncateInt32ToInt16Opcode:
                        builder.AppendLine("TRUNC_I32_TO_I16");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.BoolToUInt32Opcode:
                        builder.AppendLine("BOOL_TO_U32");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.BoolToInt32Opcode:
                        builder.AppendLine("BOOL_TO_I32");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.UInt32ToBoolOpcode:
                        builder.AppendLine("U32_TO_BOOL");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.Int32ToBoolOpcode:
                        builder.AppendLine("I32_TO_BOOL");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.JumpOpcode:
                        builder.Append("JMP ")
                               .Append(FormatBranchOperand(codeBytes, pc + 1))
                               .AppendLine();
                        pc += 3;
                        break;

                    case PlcMachineCodeAssembler.JumpIfZeroOpcode:
                        builder.Append("JZ ")
                               .Append(FormatBranchOperand(codeBytes, pc + 1))
                               .AppendLine();
                        pc += 3;
                        break;

                    case PlcMachineCodeAssembler.JumpIfNotZeroOpcode:
                        builder.Append("JNZ ")
                               .Append(FormatBranchOperand(codeBytes, pc + 1))
                               .AppendLine();
                        pc += 3;
                        break;

                    case PlcMachineCodeAssembler.RisingEdgeTriggerOpcode:
                    case PlcMachineCodeAssembler.FallingEdgeTriggerOpcode:
                        if ((pc + 2) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        var edgeOperandOffset = (uint)(pc + 1);
                        var edgeOperandValue = ReadUInt16(codeBytes, pc + 1);
                        string edgeOperandText;
                        if (relocationByOffset.TryGetValue(edgeOperandOffset, out var edgeRelocation))
                        {
                            edgeOperandText = operandFormatter != null
                                ? operandFormatter(edgeRelocation.SymbolIndex)
                                : $"sym{edgeRelocation.SymbolIndex}";
                        }
                        else
                        {
                            edgeOperandText = operandFormatter != null
                                ? operandFormatter(edgeOperandValue)
                                : edgeOperandValue.ToString();
                        }

                        builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .AppendLine(edgeOperandText);
                        pc += 3;
                        break;

                    case PlcMachineCodeAssembler.TimerOnStartOpcode:
                        if ((pc + 6) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        builder.Append("TON_START ")
                               .Append(ReadUInt16(codeBytes, pc + 1))
                               .Append(", ")
                               .Append(ReadUInt32(codeBytes, pc + 3).ToString(System.Globalization.CultureInfo.InvariantCulture))
                               .AppendLine();
                        pc += 7;
                        break;

                    case PlcMachineCodeAssembler.TimerOnDoneOpcode:
                    case PlcMachineCodeAssembler.TimerOnResetOpcode:
                    case PlcMachineCodeAssembler.TimerOnElapsedOpcode:
                    case PlcMachineCodeAssembler.TimerOnRemainingOpcode:
                    case PlcMachineCodeAssembler.TimerOffDoneOpcode:
                    case PlcMachineCodeAssembler.TimerOffResetOpcode:
                    case PlcMachineCodeAssembler.TimerPulseDoneOpcode:
                    case PlcMachineCodeAssembler.TimerPulseResetOpcode:
                    case PlcMachineCodeAssembler.CounterUpDoneOpcode:
                    case PlcMachineCodeAssembler.CounterUpValueOpcode:
                    case PlcMachineCodeAssembler.CounterUpResetOpcode:
                    case PlcMachineCodeAssembler.CounterDownDoneOpcode:
                    case PlcMachineCodeAssembler.CounterDownValueOpcode:
                    case PlcMachineCodeAssembler.CounterDownResetOpcode:
                        if ((pc + 2) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .Append(ReadUInt16(codeBytes, pc + 1).ToString(System.Globalization.CultureInfo.InvariantCulture))
                               .AppendLine();
                        pc += 3;
                        break;

                          case PlcMachineCodeAssembler.CounterUpCountOpcode:
                          case PlcMachineCodeAssembler.CounterDownCountOpcode:
                           if ((pc + 4) >= codeBytes.Length)
                           {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                           }

                           builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .Append(ReadUInt16(codeBytes, pc + 1).ToString(System.Globalization.CultureInfo.InvariantCulture))
                               .Append(", ")
                               .Append(FormatInt16Literal(ReadUInt16(codeBytes, pc + 3)))
                               .AppendLine();
                           pc += 5;
                           break;

                          case PlcMachineCodeAssembler.TimerOffStartOpcode:
                          case PlcMachineCodeAssembler.TimerPulseStartOpcode:
                           if ((pc + 6) >= codeBytes.Length)
                           {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                           }

                           builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .Append(ReadUInt16(codeBytes, pc + 1))
                               .Append(", ")
                               .Append(ReadUInt32(codeBytes, pc + 3).ToString(System.Globalization.CultureInfo.InvariantCulture))
                               .AppendLine();
                           pc += 7;
                           break;

                    case PlcMachineCodeAssembler.PushInt16Opcode:
                        if ((pc + 2) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        builder.Append("PUSH_I16 ")
                               .AppendLine(FormatInt16Literal(ReadUInt16(codeBytes, pc + 1)));
                        pc += 3;
                        break;

                    case PlcMachineCodeAssembler.PushUInt32Opcode:
                    case PlcMachineCodeAssembler.PushInt32Opcode:
                    case PlcMachineCodeAssembler.PushFloat32Opcode:
                        if ((pc + 4) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .AppendLine(FormatImmediate32(opcode, ReadUInt32(codeBytes, pc + 1)));
                        pc += 5;
                        break;

                    case PlcMachineCodeAssembler.LoadPointBoolOpcode:
                    case PlcMachineCodeAssembler.StorePointBoolOpcode:
                    case PlcMachineCodeAssembler.LoadPointInt16Opcode:
                    case PlcMachineCodeAssembler.StorePointInt16Opcode:
                    case PlcMachineCodeAssembler.LoadPointUInt32Opcode:
                    case PlcMachineCodeAssembler.StorePointUInt32Opcode:
                    case PlcMachineCodeAssembler.LoadPointInt32Opcode:
                    case PlcMachineCodeAssembler.StorePointInt32Opcode:
                    case PlcMachineCodeAssembler.LoadPointFloat32Opcode:
                    case PlcMachineCodeAssembler.StorePointFloat32Opcode:
                    case PlcMachineCodeAssembler.IncrementPointIntOpcode:
                    case PlcMachineCodeAssembler.DecrementPointIntOpcode:
                        if ((pc + 4) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        var operandOffset = (uint)(pc + 1);
                        var operandValue = ReadUInt32(codeBytes, pc + 1);
                        string operandText;
                        if (relocationByOffset.TryGetValue(operandOffset, out var relocation))
                        {
                            operandText = operandFormatter != null
                                ? operandFormatter(relocation.SymbolIndex)
                                : $"sym{relocation.SymbolIndex}";
                        }
                        else
                        {
                            operandText = operandFormatter != null
                                ? operandFormatter(operandValue)
                                : operandValue.ToString();
                        }

                           builder.Append(FormatOpcode(opcode))
                               .Append(' ')
                               .AppendLine(operandText);
                        pc += 5;
                        break;

                    default:
                    {
                        var dataStart = pc;
                        pc += 1;
                        while (pc < codeBytes.Length && !IsInstructionOpcode(codeBytes[pc]))
                        {
                            pc += 1;
                        }

                        builder.Append("DB ");
                        for (var index = dataStart; index < pc; index += 1)
                        {
                            if (index > dataStart)
                            {
                                builder.Append(", ");
                            }

                            builder.Append("0x")
                                   .Append(codeBytes[index].ToString("X2"));
                        }

                        builder.AppendLine();
                        break;
                    }
                }
            }

            return builder.ToString().TrimEnd();
        }

        private static string FormatSlotVariableVisibility(PlcAssemblySymbolFlags flags)
        {
            return (flags & PlcAssemblySymbolFlags.SlotVarPrivate) != 0
                ? "PRIVATE "
                : string.Empty;
        }

        private static string ResolveObjectSymbolName(IList<PlcAssemblySymbol> symbols, ushort symbolIndex)
        {
            return symbolIndex < symbols.Count ? symbols[symbolIndex].Name : $"sym{symbolIndex}";
        }

        private static bool IsInstructionOpcode(byte value)
        {
             return value == PlcMachineCodeAssembler.NopOpcode ||
                 value == PlcMachineCodeAssembler.HaltOpcode ||
                 value == PlcMachineCodeAssembler.PushTrueOpcode ||
                 value == PlcMachineCodeAssembler.PushFalseOpcode ||
                 value == PlcMachineCodeAssembler.DupOpcode ||
                 value == PlcMachineCodeAssembler.DropOpcode ||
                 value == PlcMachineCodeAssembler.SwapOpcode ||
                                 value == PlcMachineCodeAssembler.AndOpcode ||
                                 value == PlcMachineCodeAssembler.OrOpcode ||
                                 value == PlcMachineCodeAssembler.XorOpcode ||
                                 value == PlcMachineCodeAssembler.NotOpcode ||
                                 value == PlcMachineCodeAssembler.EqOpcode ||
                                 value == PlcMachineCodeAssembler.NeOpcode ||
                                 value == PlcMachineCodeAssembler.FloatEqualOpcode ||
                                 value == PlcMachineCodeAssembler.FloatNotEqualOpcode ||
                                 value == PlcMachineCodeAssembler.FloatLessThanOpcode ||
                                 value == PlcMachineCodeAssembler.FloatLessOrEqualOpcode ||
                                 value == PlcMachineCodeAssembler.FloatGreaterThanOpcode ||
                                 value == PlcMachineCodeAssembler.FloatGreaterOrEqualOpcode ||
                                 value == PlcMachineCodeAssembler.PushInt16Opcode ||
                                 value == PlcMachineCodeAssembler.PushUInt32Opcode ||
                                 value == PlcMachineCodeAssembler.PushInt32Opcode ||
                                 value == PlcMachineCodeAssembler.PushFloat32Opcode ||
                   value == PlcMachineCodeAssembler.LoadPointBoolOpcode ||
                   value == PlcMachineCodeAssembler.StorePointBoolOpcode ||
                                     value == PlcMachineCodeAssembler.LoadPointInt16Opcode ||
                                     value == PlcMachineCodeAssembler.StorePointInt16Opcode ||
                                     value == PlcMachineCodeAssembler.LoadPointUInt32Opcode ||
                                     value == PlcMachineCodeAssembler.StorePointUInt32Opcode ||
                                     value == PlcMachineCodeAssembler.LoadPointInt32Opcode ||
                                     value == PlcMachineCodeAssembler.StorePointInt32Opcode ||
                                     value == PlcMachineCodeAssembler.LoadPointFloat32Opcode ||
                                     value == PlcMachineCodeAssembler.StorePointFloat32Opcode ||
                   value == PlcMachineCodeAssembler.IncrementPointIntOpcode ||
                                     value == PlcMachineCodeAssembler.DecrementPointIntOpcode ||
                                     value == PlcMachineCodeAssembler.AddOpcode ||
                                     value == PlcMachineCodeAssembler.SubOpcode ||
                                     value == PlcMachineCodeAssembler.LessThanOpcode ||
                                     value == PlcMachineCodeAssembler.LessOrEqualOpcode ||
                                     value == PlcMachineCodeAssembler.GreaterThanOpcode ||
                                     value == PlcMachineCodeAssembler.GreaterOrEqualOpcode ||
                                     value == PlcMachineCodeAssembler.MinOpcode ||
                                     value == PlcMachineCodeAssembler.MaxOpcode ||
                                     value == PlcMachineCodeAssembler.ClampOpcode ||
                                     value == PlcMachineCodeAssembler.SelectOpcode ||
                                     value == PlcMachineCodeAssembler.SignExtendInt16ToInt32Opcode ||
                                     value == PlcMachineCodeAssembler.TruncateInt32ToInt16Opcode ||
                                     value == PlcMachineCodeAssembler.BoolToUInt32Opcode ||
                                     value == PlcMachineCodeAssembler.BoolToInt32Opcode ||
                                     value == PlcMachineCodeAssembler.UInt32ToBoolOpcode ||
                                     value == PlcMachineCodeAssembler.Int32ToBoolOpcode ||
                                     value == PlcMachineCodeAssembler.JumpOpcode ||
                                     value == PlcMachineCodeAssembler.JumpIfZeroOpcode ||
                                     value == PlcMachineCodeAssembler.JumpIfNotZeroOpcode ||
                                     value == PlcMachineCodeAssembler.RisingEdgeTriggerOpcode ||
                                     value == PlcMachineCodeAssembler.FallingEdgeTriggerOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOnStartOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOnDoneOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOnResetOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOnElapsedOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOnRemainingOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOffStartOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOffDoneOpcode ||
                                     value == PlcMachineCodeAssembler.TimerOffResetOpcode ||
                                     value == PlcMachineCodeAssembler.TimerPulseStartOpcode ||
                                     value == PlcMachineCodeAssembler.TimerPulseDoneOpcode ||
                                     value == PlcMachineCodeAssembler.TimerPulseResetOpcode ||
                                     value == PlcMachineCodeAssembler.CounterUpCountOpcode ||
                                     value == PlcMachineCodeAssembler.CounterUpDoneOpcode ||
                                     value == PlcMachineCodeAssembler.CounterUpValueOpcode ||
                                     value == PlcMachineCodeAssembler.CounterUpResetOpcode ||
                                     value == PlcMachineCodeAssembler.CounterDownCountOpcode ||
                                     value == PlcMachineCodeAssembler.CounterDownDoneOpcode ||
                                     value == PlcMachineCodeAssembler.CounterDownValueOpcode ||
                                     value == PlcMachineCodeAssembler.CounterDownResetOpcode;
        }

        private static string FormatOpcode(byte opcode)
        {
            switch (opcode)
            {
                case PlcMachineCodeAssembler.NopOpcode:
                    return "NOP";
                case PlcMachineCodeAssembler.HaltOpcode:
                    return "HALT";
                case PlcMachineCodeAssembler.PushTrueOpcode:
                    return "PUSH_TRUE";
                case PlcMachineCodeAssembler.PushFalseOpcode:
                    return "PUSH_FALSE";
                case PlcMachineCodeAssembler.DupOpcode:
                    return "DUP";
                case PlcMachineCodeAssembler.DropOpcode:
                    return "DROP";
                case PlcMachineCodeAssembler.SwapOpcode:
                    return "SWAP";
                case PlcMachineCodeAssembler.AndOpcode:
                    return "AND";
                case PlcMachineCodeAssembler.OrOpcode:
                    return "OR";
                case PlcMachineCodeAssembler.XorOpcode:
                    return "XOR";
                case PlcMachineCodeAssembler.NotOpcode:
                    return "NOT";
                case PlcMachineCodeAssembler.EqOpcode:
                    return "EQ";
                case PlcMachineCodeAssembler.NeOpcode:
                    return "NE";
                case PlcMachineCodeAssembler.FloatEqualOpcode:
                    return "FEQ";
                case PlcMachineCodeAssembler.FloatNotEqualOpcode:
                    return "FNE";
                case PlcMachineCodeAssembler.FloatLessThanOpcode:
                    return "FLT";
                case PlcMachineCodeAssembler.FloatLessOrEqualOpcode:
                    return "FLE";
                case PlcMachineCodeAssembler.FloatGreaterThanOpcode:
                    return "FGT";
                case PlcMachineCodeAssembler.FloatGreaterOrEqualOpcode:
                    return "FGE";
                case PlcMachineCodeAssembler.PushInt16Opcode:
                    return "PUSH_I16";
                case PlcMachineCodeAssembler.PushUInt32Opcode:
                    return "PUSH_U32";
                case PlcMachineCodeAssembler.PushInt32Opcode:
                    return "PUSH_I32";
                case PlcMachineCodeAssembler.PushFloat32Opcode:
                    return "PUSH_F32";
                case PlcMachineCodeAssembler.LoadPointBoolOpcode:
                    return "LOAD_BOOL";
                case PlcMachineCodeAssembler.StorePointBoolOpcode:
                    return "STORE_BOOL";
                case PlcMachineCodeAssembler.LoadPointInt16Opcode:
                    return "LOAD_I16";
                case PlcMachineCodeAssembler.StorePointInt16Opcode:
                    return "STORE_I16";
                case PlcMachineCodeAssembler.LoadPointUInt32Opcode:
                    return "LOAD_U32";
                case PlcMachineCodeAssembler.StorePointUInt32Opcode:
                    return "STORE_U32";
                case PlcMachineCodeAssembler.LoadPointInt32Opcode:
                    return "LOAD_I32";
                case PlcMachineCodeAssembler.StorePointInt32Opcode:
                    return "STORE_I32";
                case PlcMachineCodeAssembler.LoadPointFloat32Opcode:
                    return "LOAD_F32";
                case PlcMachineCodeAssembler.StorePointFloat32Opcode:
                    return "STORE_F32";
                case PlcMachineCodeAssembler.IncrementPointIntOpcode:
                    return "INC_INT";
                case PlcMachineCodeAssembler.DecrementPointIntOpcode:
                    return "DEC_INT";
                case PlcMachineCodeAssembler.AddOpcode:
                    return "ADD";
                case PlcMachineCodeAssembler.SubOpcode:
                    return "SUB";
                case PlcMachineCodeAssembler.LessThanOpcode:
                    return "LT";
                case PlcMachineCodeAssembler.LessOrEqualOpcode:
                    return "LE";
                case PlcMachineCodeAssembler.GreaterThanOpcode:
                    return "GT";
                case PlcMachineCodeAssembler.GreaterOrEqualOpcode:
                    return "GE";
                case PlcMachineCodeAssembler.MinOpcode:
                    return "MIN";
                case PlcMachineCodeAssembler.MaxOpcode:
                    return "MAX";
                case PlcMachineCodeAssembler.ClampOpcode:
                    return "CLAMP";
                case PlcMachineCodeAssembler.SelectOpcode:
                    return "SEL";
                case PlcMachineCodeAssembler.SignExtendInt16ToInt32Opcode:
                    return "SX_I16_TO_I32";
                case PlcMachineCodeAssembler.TruncateInt32ToInt16Opcode:
                    return "TRUNC_I32_TO_I16";
                case PlcMachineCodeAssembler.BoolToUInt32Opcode:
                    return "BOOL_TO_U32";
                case PlcMachineCodeAssembler.BoolToInt32Opcode:
                    return "BOOL_TO_I32";
                case PlcMachineCodeAssembler.UInt32ToBoolOpcode:
                    return "U32_TO_BOOL";
                case PlcMachineCodeAssembler.Int32ToBoolOpcode:
                    return "I32_TO_BOOL";
                case PlcMachineCodeAssembler.JumpOpcode:
                    return "JMP";
                case PlcMachineCodeAssembler.JumpIfZeroOpcode:
                    return "JZ";
                case PlcMachineCodeAssembler.JumpIfNotZeroOpcode:
                    return "JNZ";
                case PlcMachineCodeAssembler.RisingEdgeTriggerOpcode:
                    return "R_TRIG";
                case PlcMachineCodeAssembler.FallingEdgeTriggerOpcode:
                    return "F_TRIG";
                case PlcMachineCodeAssembler.TimerOnStartOpcode:
                    return "TON_START";
                case PlcMachineCodeAssembler.TimerOnDoneOpcode:
                    return "TON_DONE";
                case PlcMachineCodeAssembler.TimerOnResetOpcode:
                    return "TON_RESET";
                case PlcMachineCodeAssembler.TimerOnElapsedOpcode:
                    return "TON_ELAPSED";
                case PlcMachineCodeAssembler.TimerOnRemainingOpcode:
                    return "TON_REMAINING";
                case PlcMachineCodeAssembler.TimerOffStartOpcode:
                    return "TOF_START";
                case PlcMachineCodeAssembler.TimerOffDoneOpcode:
                    return "TOF_DONE";
                case PlcMachineCodeAssembler.TimerOffResetOpcode:
                    return "TOF_RESET";
                case PlcMachineCodeAssembler.TimerPulseStartOpcode:
                    return "TP_START";
                case PlcMachineCodeAssembler.TimerPulseDoneOpcode:
                    return "TP_DONE";
                case PlcMachineCodeAssembler.TimerPulseResetOpcode:
                    return "TP_RESET";
                case PlcMachineCodeAssembler.CounterUpCountOpcode:
                    return "CTU_COUNT";
                case PlcMachineCodeAssembler.CounterUpDoneOpcode:
                    return "CTU_DONE";
                case PlcMachineCodeAssembler.CounterUpValueOpcode:
                    return "CTU_VALUE";
                case PlcMachineCodeAssembler.CounterUpResetOpcode:
                    return "CTU_RESET";
                case PlcMachineCodeAssembler.CounterDownCountOpcode:
                    return "CTD_COUNT";
                case PlcMachineCodeAssembler.CounterDownDoneOpcode:
                    return "CTD_DONE";
                case PlcMachineCodeAssembler.CounterDownValueOpcode:
                    return "CTD_VALUE";
                case PlcMachineCodeAssembler.CounterDownResetOpcode:
                    return "CTD_RESET";
                default:
                    return "DB";
            }
        }

        private static string FormatBranchOperand(byte[] codeBytes, int operandOffset)
        {
            if ((operandOffset + 1) >= codeBytes.Length)
            {
                return "0";
            }

            var offset = unchecked((short)ReadUInt16(codeBytes, operandOffset));
            return offset.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        private static string FormatInt16Literal(ushort rawValue)
        {
            return ((short)rawValue).ToString();
        }

        private static string FormatImmediate32(byte opcode, uint rawValue)
        {
            if (opcode == PlcMachineCodeAssembler.PushFloat32Opcode)
            {
                return BitConverter.ToSingle(BitConverter.GetBytes(rawValue), 0)
                    .ToString("R", System.Globalization.CultureInfo.InvariantCulture);
            }

            if (opcode == PlcMachineCodeAssembler.PushInt32Opcode)
            {
                return unchecked((int)rawValue).ToString(System.Globalization.CultureInfo.InvariantCulture);
            }

            return rawValue.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }

        private static string FormatValueType(byte rawType)
        {
            if (rawType == byte.MaxValue)
            {
                return "UNKNOWN";
            }

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

        private static void EnsureRange(uint offset, uint size, uint totalSize, string sectionName)
        {
            if (offset < PlcObjectFileBuilder.ObjectFileHeaderSize || offset + size > totalSize)
            {
                throw new PlcObjectFileParseException($"Object file {sectionName} is out of bounds.");
            }
        }

        private static string ReadFixedAscii(byte[] data, int offset, int length)
        {
            var count = 0;
            while (count < length && data[offset + count] != 0)
            {
                count += 1;
            }

            return count > 0 ? Encoding.ASCII.GetString(data, offset, count) : string.Empty;
        }

        private static ushort ReadUInt16(byte[] data, int offset)
        {
            return (ushort)(data[offset + 0] | (data[offset + 1] << 8));
        }

        private static uint ReadUInt32(byte[] data, int offset)
        {
            return (uint)(data[offset + 0] |
                         (data[offset + 1] << 8) |
                         (data[offset + 2] << 16) |
                         (data[offset + 3] << 24));
        }

        private static string BuildPointPath(string deviceId, string feature, string pointId)
        {
            if (string.IsNullOrWhiteSpace(deviceId) || string.IsNullOrWhiteSpace(feature) || string.IsNullOrWhiteSpace(pointId))
            {
                return string.Empty;
            }

            return deviceId + "." + feature + "." + pointId;
        }
    }
}