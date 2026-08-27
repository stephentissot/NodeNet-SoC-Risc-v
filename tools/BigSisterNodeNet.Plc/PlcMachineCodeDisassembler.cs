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

        public static string DisassembleBytecode(byte[] codeBytes, Func<ushort, string> operandFormatter)
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

            var totalSize = ReadUInt32(objectFileBytes, 10);
            if (totalSize > objectFileBytes.Length)
            {
                throw new PlcObjectFileParseException("Object file total size exceeds the provided payload length.");
            }

            var codeSize = ReadUInt32(objectFileBytes, 14);
            var symbolCount = ReadUInt16(objectFileBytes, 22);
            var relocationCount = ReadUInt16(objectFileBytes, 24);
            var symbolTableOffset = ReadUInt32(objectFileBytes, 26);
            var relocationTableOffset = ReadUInt32(objectFileBytes, 30);

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
                var keyword = symbol.Kind == PlcObjectSymbolKind.ParamPointId ? "PARAM" : "CONST";
                if (symbol.Kind == PlcObjectSymbolKind.ParamPointId)
                {
                    builder.Append(keyword)
                           .Append(" POINT_ID ")
                           .Append(symbol.Name)
                           .AppendLine();
                }
                else
                {
                    builder.Append(keyword)
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
                                                   symbolIndex => ResolveObjectSymbolName(result.Symbols, symbolIndex)));
            return builder.ToString().TrimEnd();
        }

        private static string BuildInstructionListing(byte[] codeBytes,
                                                      IList<PlcAssemblyRelocation> relocations,
                                                      Func<ushort, string> operandFormatter)
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
                    case PlcMachineCodeAssembler.HaltOpcode:
                        builder.AppendLine("HALT");
                        pc += 1;
                        break;

                    case PlcMachineCodeAssembler.LoadPointBoolOpcode:
                    case PlcMachineCodeAssembler.StorePointBoolOpcode:
                        if ((pc + 2) >= codeBytes.Length)
                        {
                            throw new PlcObjectFileParseException($"Truncated operand for opcode 0x{opcode:X2} at offset {pc}.");
                        }

                        var operandOffset = (uint)(pc + 1);
                        var operandValue = ReadUInt16(codeBytes, pc + 1);
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

                        builder.Append(opcode == PlcMachineCodeAssembler.LoadPointBoolOpcode ? "LOAD_BOOL " : "STORE_BOOL ")
                               .AppendLine(operandText);
                        pc += 3;
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

        private static string ResolveObjectSymbolName(IList<PlcAssemblySymbol> symbols, ushort symbolIndex)
        {
            return symbolIndex < symbols.Count ? symbols[symbolIndex].Name : $"sym{symbolIndex}";
        }

        private static bool IsInstructionOpcode(byte value)
        {
            return value == PlcMachineCodeAssembler.HaltOpcode ||
                   value == PlcMachineCodeAssembler.LoadPointBoolOpcode ||
                   value == PlcMachineCodeAssembler.StorePointBoolOpcode;
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