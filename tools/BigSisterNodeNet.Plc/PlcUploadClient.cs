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
        public IDictionary<string, object> UploadStatusResponse { get; set; }
    }

    public sealed class PlcDownloadResult
    {
        public ushort SlotId { get; set; }
        public byte[] BytecodeBytes { get; set; } = Array.Empty<byte>();
        public byte[] PayloadBytes { get; set; } = Array.Empty<byte>();
        public string ArtifactType { get; set; } = string.Empty;
        public IDictionary<string, object> FinalResponse { get; set; }
    }

    public sealed class PlcEraseResult
    {
        public ushort SlotId { get; set; }
        public bool RebootPersistent { get; set; }
        public IDictionary<string, object> Response { get; set; }
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

        public IDictionary<string, object> CreateStatusRequest(PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcUploadStatusReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
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

        public IDictionary<string, object> CreateEraseRequest(ushort slotId, PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return new Dictionary<string, object>
            {
                ["cmd"] = "plcEraseReq",
                ["from"] = options.LocalAddress,
                ["to"] = options.RemoteAddress,
                ["slotId"] = slotId,
                ["persistToFlash"] = options.PersistToFlash,
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
                var loadStatusText = DescribeLoadStatus(loadStatus);
                var message = string.IsNullOrWhiteSpace(error)
                    ? "Device returned a rejected response."
                    : string.IsNullOrWhiteSpace(loadStatus)
                        ? $"Device returned error '{error}'."
                        : $"Device returned error '{error}' ({loadStatusText}).";
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

        public static string BuildUiStatusMessage(ushort slotId,
                                                  IDictionary<string, object> commitResponse,
                                                  IDictionary<string, object> uploadStatusResponse)
        {
            var rebootPersistent = ReadResponseBoolean(commitResponse, "rebootPersistent");
            var persistenceText = rebootPersistent ? "Persisté pour reboot." : "Chargé en runtime uniquement.";
            var autoLoadPending = ReadResponseBoolean(commitResponse, "autoLoadPending");

            if (!autoLoadPending)
            {
                return $"Upload terminé sur le slot {slotId}. {persistenceText}";
            }

            if (uploadStatusResponse == null)
            {
                return $"Upload terminé sur le slot {slotId}. Auto-load différé en attente. {persistenceText}";
            }

            if (ReadResponseBoolean(uploadStatusResponse, "autoLoadInProgress"))
            {
                return $"Upload terminé sur le slot {slotId}. Auto-load différé en cours. {persistenceText}";
            }

            if (!ReadResponseBoolean(uploadStatusResponse, "lastAutoLoadValid"))
            {
                return $"Upload terminé sur le slot {slotId}. Auto-load différé terminé sans diagnostic final. {persistenceText}";
            }

            var autoLoadSlotId = ReadResponseUInt16(uploadStatusResponse, "lastAutoLoadSlotId");
            if (autoLoadSlotId != slotId)
            {
                return $"Upload terminé sur le slot {slotId}. Auto-load différé en cours. {persistenceText}";
            }

            var autoLoadStatus = ReadResponseString(uploadStatusResponse, "lastAutoLoadStatus");
            if (string.Equals(autoLoadStatus, "0", StringComparison.Ordinal))
            {
                return $"Upload terminé sur le slot {slotId}. Auto-load différé réussi. {persistenceText}";
            }

            return $"Upload terminé sur le slot {slotId}, mais l'auto-load différé a échoué: {DescribeAutoLoadFailure(uploadStatusResponse)} {persistenceText}";
        }

        public static string BuildUiEraseStatusMessage(ushort slotId, IDictionary<string, object> response)
        {
            if (ReadResponseBoolean(response, "erasePending"))
            {
                return $"Effacement du slot {slotId} accepté. Finalisation différée en cours.";
            }

            var rebootPersistent = ReadResponseBoolean(response, "rebootPersistent");
            var persistenceText = rebootPersistent ? "Suppression persistée pour reboot." : "Suppression runtime uniquement.";
            return $"Slot {slotId} effacé. {persistenceText}";
        }

        public static string DescribeAutoLoadFailure(IDictionary<string, object> response)
        {
            if (response == null)
            {
                return "diagnostic indisponible.";
            }

            var loadStatus = DescribeLoadStatus(ReadResponseString(response, "lastAutoLoadStatus"));
            var parseStatus = DescribeParseStatus(ReadResponseString(response, "lastAutoLoadParseStatus"));
            var linkStatus = DescribeLinkStatus(ReadResponseString(response, "lastAutoLoadLinkStatus"));
            var resolveStatus = DescribeResolveStatus(ReadResponseString(response, "lastAutoLoadResolveStatus"));
            var failingSymbol = ReadResponseString(response, "lastAutoLoadFailingSymbolIndex");
            var failingRelocation = ReadResponseString(response, "lastAutoLoadFailingRelocationIndex");

            var details = new StringBuilder();
            details.Append(loadStatus);

            if (!string.IsNullOrWhiteSpace(resolveStatus) && !string.Equals(resolveStatus, "resolveStatus=0 (résolu)", StringComparison.Ordinal))
            {
                details.Append($", {resolveStatus}");
            }
            else if (!string.IsNullOrWhiteSpace(linkStatus) && !string.Equals(linkStatus, "linkStatus=0 (ok)", StringComparison.Ordinal))
            {
                details.Append($", {linkStatus}");
            }
            else if (!string.IsNullOrWhiteSpace(parseStatus) && !string.Equals(parseStatus, "parseStatus=0 (ok)", StringComparison.Ordinal))
            {
                details.Append($", {parseStatus}");
            }

            if (TryParseUInt16(failingSymbol, out var symbolIndex) && symbolIndex != ushort.MaxValue)
            {
                details.Append($", symbole #{symbolIndex}");
            }

            if (TryParseUInt16(failingRelocation, out var relocationIndex) && relocationIndex != ushort.MaxValue)
            {
                details.Append($", relocation #{relocationIndex}");
            }

            return details.ToString() + ".";
        }

        public static string DescribeLoadStatus(string rawValue)
        {
            return DescribeStatus(rawValue, "loadStatus", value =>
            {
                switch (value)
                {
                    case 0: return "ok";
                    case 1: return "argument invalide";
                    case 2: return "région indisponible";
                    case 3: return "slot hors plage";
                    case 4: return "bytecode trop grand";
                    case 5: return "parse objet échoué";
                    case 6: return "échec de link/load";
                    case 7: return "ABI incompatible";
                    case 8: return "checksum invalide";
                    case 9: return "lecture flash échouée";
                    case 10: return "paramètres trop volumineux";
                    case 11: return "opcode non supporté";
                    default: return null;
                }
            });
        }

        public static string DescribeParseStatus(string rawValue)
        {
            return DescribeStatus(rawValue, "parseStatus", value =>
            {
                switch (value)
                {
                    case 0: return "ok";
                    case 1: return "argument invalide";
                    case 2: return "header hors plage";
                    case 3: return "magic invalide";
                    case 4: return "version non supportée";
                    case 5: return "code hors plage";
                    case 6: return "entry offset hors plage";
                    case 7: return "table des symboles hors plage";
                    case 8: return "table des relocations hors plage";
                    default: return null;
                }
            });
        }

        public static string DescribeLinkStatus(string rawValue)
        {
            return DescribeStatus(rawValue, "linkStatus", value =>
            {
                switch (value)
                {
                    case 0: return "ok";
                    case 1: return "argument invalide";
                    case 2: return "buffer de sortie trop petit";
                    case 3: return "entry offset hors plage";
                    case 4: return "index symbole hors plage";
                    case 5: return "relocation hors plage";
                    case 6: return "relocation non supportée";
                    case 7: return "résolution symbole échouée";
                    default: return null;
                }
            });
        }

        public static string DescribeResolveStatus(string rawValue)
        {
            return DescribeStatus(rawValue, "resolveStatus", value =>
            {
                switch (value)
                {
                    case 0: return "résolu";
                    case 1: return "point introuvable";
                    case 2: return "type runtime non supporté";
                    case 3: return "mismatch de type";
                    case 4: return "accès refusé";
                    default: return null;
                }
            });
        }

        private static string DescribeStatus(string rawValue, string fieldName, Func<int, string> describe)
        {
            if (string.IsNullOrWhiteSpace(rawValue))
            {
                return string.Empty;
            }

            if (!int.TryParse(rawValue, out var value))
            {
                return $"{fieldName}={rawValue}";
            }

            var description = describe(value);
            return string.IsNullOrWhiteSpace(description)
                ? $"{fieldName}={value}"
                : $"{fieldName}={value} ({description})";
        }

        private static bool ReadResponseBoolean(IDictionary<string, object> response, string key)
        {
            return response != null &&
                   response.TryGetValue(key, out var value) &&
                   value != null &&
                   Convert.ToBoolean(value);
        }

        private static string ReadResponseString(IDictionary<string, object> response, string key)
        {
            return response != null && response.TryGetValue(key, out var value) && value != null
                ? Convert.ToString(value)
                : string.Empty;
        }

        private static ushort ReadResponseUInt16(IDictionary<string, object> response, string key)
        {
            if (response == null || !response.TryGetValue(key, out var value) || value == null)
            {
                return 0;
            }

            try
            {
                return Convert.ToUInt16(value);
            }
            catch
            {
                return 0;
            }
        }

        private static bool TryParseUInt16(string text, out ushort value)
        {
            return ushort.TryParse(text, out value);
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