using System;
using System.Collections.Generic;
using System.Text;

namespace BigSisterNodeNet.Plc
{
    public sealed class PlcUploadOptions
    {
        public byte LocalAddress { get; set; } = 255;
        public byte RemoteAddress { get; set; }
        public ushort SlotId { get; set; }
        public bool PersistToFlash { get; set; } = true;
        public bool AutoLoad { get; set; } = true;
        public TimeSpan ResponseTimeout { get; set; } = TimeSpan.FromSeconds(3);
        public PlcObjectFileOptions ObjectFileOptions { get; set; } = new PlcObjectFileOptions();
    }

    public sealed class PlcUploadResult
    {
        public uint UploadId { get; set; }
        public IDictionary<string, object> BeginResponse { get; set; }
        public IDictionary<string, object> CommitResponse { get; set; }
    }

    public sealed class PlcDownloadResult
    {
        public ushort SlotId { get; set; }
        public byte[] BytecodeBytes { get; set; } = Array.Empty<byte>();
        public byte[] PayloadBytes { get; set; } = Array.Empty<byte>();
        public string ArtifactType { get; set; } = string.Empty;
        public IDictionary<string, object> FinalResponse { get; set; }
    }

    public sealed class PlcDeviceErrorException : InvalidOperationException
    {
        public PlcDeviceErrorException(string message,
                                       string errorCode,
                                       string loadStatus,
                                       IDictionary<string, object> response)
            : base(message)
        {
            ErrorCode = errorCode ?? string.Empty;
            LoadStatus = loadStatus ?? string.Empty;
            Response = response;
        }

        public string ErrorCode { get; }
        public string LoadStatus { get; }
        public IDictionary<string, object> Response { get; }
    }

    public sealed class PlcUploadClient
    {
        public byte[] BuildObjectFile(string machineCode, PlcUploadOptions options)
        {
            if (machineCode == null)
            {
                throw new ArgumentNullException(nameof(machineCode));
            }

            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return PlcObjectFileBuilder.BuildFromMachineCode(machineCode, options.ObjectFileOptions ?? new PlcObjectFileOptions());
        }

        public IDictionary<string, object> CreateBeginRequest(byte[] objectFileBytes, PlcUploadOptions options)
        {
            if (objectFileBytes == null)
            {
                throw new ArgumentNullException(nameof(objectFileBytes));
            }

            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcUploadBeginReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["slotId"] = options.SlotId,
                ["artifactType"] = "objectFileV1",
                ["totalSize"] = objectFileBytes.Length,
                ["payloadCrc32"] = PlcObjectFileBuilder.ComputeChecksum32(objectFileBytes),
                ["persistToFlash"] = options.PersistToFlash,
                ["autoLoad"] = options.AutoLoad,
            };
        }

        public IDictionary<string, object> CreateCommitRequest(uint uploadId, PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcUploadCommitReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["uploadId"] = uploadId,
            };
        }

        public IDictionary<string, object> CreateDataRequest(uint uploadId,
                                                             uint offset,
                                                             byte[] payloadSource,
                                                             int payloadOffset,
                                                             int payloadCount,
                                                             PlcUploadOptions options)
        {
            if (payloadSource == null)
            {
                throw new ArgumentNullException(nameof(payloadSource));
            }

            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcUploadDataReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["uploadId"] = uploadId,
                ["offset"] = offset,
                ["dataBase64"] = Convert.ToBase64String(payloadSource, payloadOffset, payloadCount),
            };
        }

        public IDictionary<string, object> CreateBytecodeReadRequest(ushort slotId,
                                                                      uint offset,
                                                                      int requestedCount,
                                                                      PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcBytecodeReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["slotId"] = slotId,
                ["offset"] = offset,
                ["maxBytes"] = requestedCount,
            };
        }

        public IDictionary<string, object> CreateObjectFileReadRequest(ushort slotId,
                                                                        uint offset,
                                                                        int requestedCount,
                                                                        PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcObjectFileReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["slotId"] = slotId,
                ["offset"] = offset,
                ["maxBytes"] = requestedCount,
            };
        }

        public byte[] BuildDataFrame(uint uploadId, uint offset, byte[] payloadSource, int payloadOffset, int payloadCount)
        {
            var frame = new byte[16 + payloadCount];
            frame[0] = 0xA5;
            frame[1] = 0x01;
            frame[2] = 0x00;
            frame[3] = 0x00;
            WriteUInt32(frame, 4, uploadId);
            WriteUInt32(frame, 8, offset);
            WriteUInt16(frame, 12, (ushort)payloadCount);
            WriteUInt16(frame, 14, PlcObjectFileBuilder.ComputeChecksum16(payloadSource, payloadOffset, payloadCount));
            Buffer.BlockCopy(payloadSource, payloadOffset, frame, 16, payloadCount);
            return frame;
        }

        public IDictionary<string, object> ExpectOk(IDictionary<string, object> response, string expectedCommand)
        {
            if (response == null)
            {
                throw new InvalidOperationException("Received an empty response from the device.");
            }

            if (!string.Equals(ReadString(response, "cmd"), expectedCommand, StringComparison.Ordinal))
            {
                throw new InvalidOperationException($"Unexpected response command. Expected '{expectedCommand}'.");
            }

            if (!ReadBoolean(response, "ok"))
            {
                var error = ReadString(response, "error");
                var loadStatus = response.TryGetValue("loadStatus", out var loadStatusValue) && loadStatusValue != null
                    ? Convert.ToString(loadStatusValue)
                    : string.Empty;
                var message = string.IsNullOrWhiteSpace(error)
                    ? "Device returned a rejected response."
                    : string.IsNullOrWhiteSpace(loadStatus)
                        ? $"Device returned error '{error}'."
                        : $"Device returned error '{error}' (loadStatus={loadStatus}).";
                throw new PlcDeviceErrorException(message, error, loadStatus, response);
            }

            return response;
        }

        public string ReadString(IDictionary<string, object> response, string key)
        {
            return response.TryGetValue(key, out var value) && value != null ? Convert.ToString(value) : string.Empty;
        }

        public bool ReadBoolean(IDictionary<string, object> response, string key)
        {
            return response.TryGetValue(key, out var value) && value != null && Convert.ToBoolean(value);
        }

        public int ReadInt32(IDictionary<string, object> response, string key)
        {
            if (!response.TryGetValue(key, out var value) || value == null)
            {
                throw new InvalidOperationException($"Response is missing required field '{key}'.");
            }

            return Convert.ToInt32(value);
        }

        public uint ReadUInt32(IDictionary<string, object> response, string key)
        {
            if (!response.TryGetValue(key, out var value) || value == null)
            {
                throw new InvalidOperationException($"Response is missing required field '{key}'.");
            }

            return Convert.ToUInt32(value);
        }

        public byte[] ReadBase64Bytes(IDictionary<string, object> response, string key)
        {
            if (response == null)
            {
                throw new ArgumentNullException(nameof(response));
            }

            if (!response.TryGetValue(key, out var value) || value == null)
            {
                return Array.Empty<byte>();
            }

            var text = Convert.ToString(value);
            if (string.IsNullOrWhiteSpace(text))
            {
                return Array.Empty<byte>();
            }

            return Convert.FromBase64String(text);
        }

        private static void WriteUInt16(byte[] buffer, int offset, ushort value)
        {
            buffer[offset + 0] = (byte)(value & 0xFFu);
            buffer[offset + 1] = (byte)((value >> 8) & 0xFFu);
        }

        private static void WriteUInt32(byte[] buffer, int offset, uint value)
        {
            buffer[offset + 0] = (byte)(value & 0xFFu);
            buffer[offset + 1] = (byte)((value >> 8) & 0xFFu);
            buffer[offset + 2] = (byte)((value >> 16) & 0xFFu);
            buffer[offset + 3] = (byte)((value >> 24) & 0xFFu);
        }

        private static string EncodeHex(byte[] buffer, int offset, int count)
        {
            if (count <= 0)
            {
                return string.Empty;
            }

            var builder = new StringBuilder(count * 2);
            for (var index = 0; index < count; index += 1)
            {
                builder.Append(buffer[offset + index].ToString("X2"));
            }

            return builder.ToString();
        }
    }
}