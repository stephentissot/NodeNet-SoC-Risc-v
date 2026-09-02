using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace BigSisterNodeNet.Plc
{
    public enum PlcObjectSymbolKind : byte
    {
        ConstPointId = 0,
        ParamPointId = 1,
        SlotVar = 2,
    }

    public enum PlcValueType : byte
    {
        Bool = 0,
        Uint16 = 1,
        Int16 = 2,
        Uint32 = 3,
        Int32 = 4,
        Float = 5,
        Enum = 6,
        String = 7,
    }

    public enum PlcRuntimeLinkAccess : byte
    {
        Read = 0,
        Write = 1,
        ReadWrite = 2,
    }

    [Flags]
    public enum PlcAssemblySymbolFlags : byte
    {
        None = 0,
        SlotVarPrivate = 1 << 0,
    }

    public sealed class PlcObjectFileOptions
    {
        public ushort AbiVersion { get; set; } = 1;
        public uint Flags { get; set; } = 0;
        public uint EntryOffset { get; set; } = 0;
        public uint MaxInstructionsPerScan { get; set; } = 200;
        public uint MaxScanTimeUs { get; set; } = 10000;
        public uint RuntimeHeaderAddress { get; set; } = 0x20100000u;
        public IDictionary<string, string> PointBindings { get; set; } = new Dictionary<string, string>(StringComparer.Ordinal);
    }

    public sealed class PlcAssemblySymbol
    {
        public string Name { get; set; }
        public PlcObjectSymbolKind Kind { get; set; }
        public string PointPath { get; set; }
        public byte ExpectedType { get; set; } = byte.MaxValue;
        public PlcRuntimeLinkAccess Access { get; set; }
        public PlcAssemblySymbolFlags Flags { get; set; }
    }

    public sealed class PlcAssemblyRelocation
    {
        public uint CodeOffset { get; set; }
        public ushort SymbolIndex { get; set; }
        public byte RelocationKind { get; set; } = 0;
    }

    public sealed class PlcAssemblyResult
    {
        public byte[] CodeBytes { get; set; } = Array.Empty<byte>();
        public IList<PlcAssemblySymbol> Symbols { get; } = new List<PlcAssemblySymbol>();
        public IList<PlcAssemblyRelocation> Relocations { get; } = new List<PlcAssemblyRelocation>();
    }

    public static class PlcObjectFileBuilder
    {
        public const uint ObjectFileMagic = 0x314A424Fu;
        public const ushort ObjectFileVersion = 1;
        public const int ObjectFileHeaderSize = 52;
        public const int SymbolNameSize = 16;
        public const int DeviceIdSize = 16;
        public const int FeatureSize = 32;
        public const int PointIdSize = 32;
        public const int SymbolRecordSize = 102;
        public const int RelocationRecordSize = 8;

        public static byte[] BuildFromMachineCode(string source, PlcObjectFileOptions options)
        {
            return Build(PlcMachineCodeAssembler.Assemble(source, options), options);
        }

        public static byte[] Build(PlcAssemblyResult assembly, PlcObjectFileOptions options)
        {
            if (assembly == null)
            {
                throw new ArgumentNullException(nameof(assembly));
            }

            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            var codeBytes = assembly.CodeBytes ?? Array.Empty<byte>();
            if (options.EntryOffset > (uint)codeBytes.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(options), "Entry offset exceeds code size.");
            }

            var symbolTableOffset = ObjectFileHeaderSize + codeBytes.Length;
            var relocationTableOffset = symbolTableOffset + (assembly.Symbols.Count * SymbolRecordSize);
            var totalSize = relocationTableOffset + (assembly.Relocations.Count * RelocationRecordSize);

            using (var stream = new MemoryStream(totalSize))
            using (var writer = new BinaryWriter(stream, Encoding.ASCII))
            {
                writer.Write(ObjectFileMagic);
                writer.Write(ObjectFileVersion);
                writer.Write(options.AbiVersion);
                writer.Write(options.Flags);
                writer.Write((uint)totalSize);
                writer.Write((uint)codeBytes.Length);
                writer.Write(options.EntryOffset);
                writer.Write((ushort)assembly.Symbols.Count);
                writer.Write((ushort)assembly.Relocations.Count);
                writer.Write((uint)symbolTableOffset);
                writer.Write((uint)relocationTableOffset);
                writer.Write(options.MaxInstructionsPerScan);
                writer.Write(options.MaxScanTimeUs);
                writer.Write(options.RuntimeHeaderAddress);
                writer.Write(0u);

                writer.Write(codeBytes);

                foreach (var symbol in assembly.Symbols)
                {
                    WriteFixedAscii(writer, symbol.Name, SymbolNameSize);
                    writer.Write((byte)symbol.Kind);
                    writer.Write((byte)symbol.Flags);

                    if (symbol.Kind == PlcObjectSymbolKind.SlotVar)
                    {
                        WriteFixedAscii(writer, string.Empty, DeviceIdSize);
                        WriteFixedAscii(writer, string.Empty, FeatureSize);
                        WriteFixedAscii(writer, string.Empty, PointIdSize);
                    }
                    else
                    {
                        var pointIdentity = ParsePointPath(symbol.PointPath);
                        WriteFixedAscii(writer, pointIdentity.DeviceId, DeviceIdSize);
                        WriteFixedAscii(writer, pointIdentity.Feature, FeatureSize);
                        WriteFixedAscii(writer, pointIdentity.PointId, PointIdSize);
                    }
                    writer.Write(symbol.ExpectedType);
                    writer.Write((byte)symbol.Access);
                    writer.Write((ushort)0);
                }

                foreach (var relocation in assembly.Relocations)
                {
                    writer.Write(relocation.CodeOffset);
                    writer.Write(relocation.SymbolIndex);
                    writer.Write(relocation.RelocationKind);
                    writer.Write((byte)0);
                }

                writer.Flush();

                var data = stream.ToArray();
                var checksum = ComputeChecksum32(data, ObjectFileHeaderSize, data.Length - ObjectFileHeaderSize);
                WriteUInt32Le(data, 48, checksum);
                return data;
            }
        }

        public static uint ComputeChecksum32(byte[] data)
        {
            if (data == null)
            {
                throw new ArgumentNullException(nameof(data));
            }

            return ComputeChecksum32(data, 0, data.Length);
        }

        public static uint ComputeChecksum32(byte[] data, int offset, int count)
        {
            if (data == null)
            {
                throw new ArgumentNullException(nameof(data));
            }

            if (offset < 0 || count < 0 || offset + count > data.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(offset));
            }

            uint value = 2166136261u;
            for (var index = 0; index < count; index += 1)
            {
                value ^= data[offset + index];
                value *= 16777619u;
            }

            return value;
        }

        public static ushort ComputeChecksum16(byte[] data, int offset, int count)
        {
            if (data == null)
            {
                throw new ArgumentNullException(nameof(data));
            }

            if (offset < 0 || count < 0 || offset + count > data.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(offset));
            }

            uint sum = 0u;
            for (var index = 0; index < count; index += 1)
            {
                sum = (sum + data[offset + index]) & 0xFFFFu;
            }

            return (ushort)sum;
        }

        private static void WriteFixedAscii(BinaryWriter writer, string value, int capacity)
        {
            var bytes = new byte[capacity];
            if (!string.IsNullOrEmpty(value))
            {
                var encoded = Encoding.ASCII.GetBytes(value);
                if (encoded.Length >= capacity)
                {
                    throw new InvalidOperationException($"Value '{value}' exceeds fixed field size {capacity - 1}.");
                }

                Buffer.BlockCopy(encoded, 0, bytes, 0, encoded.Length);
            }

            writer.Write(bytes);
        }

        private static void WriteUInt32Le(byte[] buffer, int offset, uint value)
        {
            buffer[offset + 0] = (byte)(value & 0xFFu);
            buffer[offset + 1] = (byte)((value >> 8) & 0xFFu);
            buffer[offset + 2] = (byte)((value >> 16) & 0xFFu);
            buffer[offset + 3] = (byte)((value >> 24) & 0xFFu);
        }

        private static PlcPointIdentityParts ParsePointPath(string pointPath)
        {
            if (string.IsNullOrWhiteSpace(pointPath))
            {
                throw new InvalidOperationException("Point path is required.");
            }

            var firstDot = pointPath.IndexOf('.');
            var lastDot = pointPath.LastIndexOf('.');
            if (firstDot <= 0 || lastDot <= firstDot + 1 || lastDot >= pointPath.Length - 1)
            {
                throw new InvalidOperationException($"Point path '{pointPath}' must follow deviceId.feature.pointId.");
            }

            return new PlcPointIdentityParts
            {
                DeviceId = pointPath.Substring(0, firstDot),
                Feature = pointPath.Substring(firstDot + 1, lastDot - firstDot - 1),
                PointId = pointPath.Substring(lastDot + 1),
            };
        }

        private sealed class PlcPointIdentityParts
        {
            public string DeviceId { get; set; }
            public string Feature { get; set; }
            public string PointId { get; set; }
        }
    }
}