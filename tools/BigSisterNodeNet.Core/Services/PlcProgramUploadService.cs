using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Plc;
using System;
using System.Collections.Generic;
using System.IO;

namespace BigSisterNodeNet.Core.Services
{
    public class PlcProgramUploadService : IPlcProgramUploadService
    {
        private const int BytecodeReadChunkSize = 768;
        private static readonly TimeSpan PersistedCommitResponseTimeout = TimeSpan.FromSeconds(20);
        private static readonly TimeSpan DeferredStatusResponseTimeout = TimeSpan.FromSeconds(5);
        private static readonly TimeSpan DeferredStatusObservationWindow = TimeSpan.FromSeconds(12);

        private readonly NodeNetCore _nodeNetCore;
        private readonly PlcUploadClient _uploadClient = new PlcUploadClient();

        public PlcProgramUploadService(NodeNetCore nodeNetCore)
        {
            _nodeNetCore = nodeNetCore ?? throw new ArgumentNullException(nameof(nodeNetCore));
        }

        public PlcUploadResult UploadProgram(NodeNet_SOC node, string program, ushort slot, PlcObjectFileOptions objectFileOptions = null)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            if (string.IsNullOrWhiteSpace(program))
            {
                throw new ArgumentException("Program source is required.", nameof(program));
            }

            var options = new PlcUploadOptions
            {
                LocalAddress = NodeNetAddress.SerialEndpoint,
                RemoteAddress = node.Address,
                SlotId = slot,
                PersistToFlash = true,
                ObjectFileOptions = objectFileOptions ?? new PlcObjectFileOptions(),
            };

            var objectFileBytes = _uploadClient.BuildObjectFile(program, options);
            return _nodeNetCore.ExecuteExclusiveTransportSession(transport => UploadObjectFile(transport, objectFileBytes, options));
        }

        public PlcDownloadResult DownloadProgramBytecode(NodeNet_SOC node, ushort slot)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            var options = new PlcUploadOptions
            {
                LocalAddress = NodeNetAddress.SerialEndpoint,
                RemoteAddress = node.Address,
                SlotId = slot,
            };

            return _nodeNetCore.ExecuteExclusiveTransportSession(transport => DownloadBytecode(transport, options));
        }

        public PlcDownloadResult DownloadProgramObjectFile(NodeNet_SOC node, ushort slot)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            var options = new PlcUploadOptions
            {
                LocalAddress = NodeNetAddress.SerialEndpoint,
                RemoteAddress = node.Address,
                SlotId = slot,
            };

            return _nodeNetCore.ExecuteExclusiveTransportSession(transport => DownloadObjectFile(transport, options));
        }

        private PlcUploadResult UploadObjectFile(Transport.ISerialTransport transport, byte[] objectFileBytes, PlcUploadOptions options)
        {
            if (transport == null)
            {
                throw new ArgumentNullException(nameof(transport));
            }

            if (!transport.IsOpen)
            {
                throw new InvalidOperationException("The NodeNet serial transport must be started before uploading a PLC program.");
            }

            var beginResponse = WaitForAcceptedResponse(
                "plcUploadBeginRes",
                options,
                message => true,
                () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateBeginRequest(objectFileBytes, options))));
            var uploadId = _uploadClient.ReadUInt32(beginResponse, "uploadId");
            var acceptedChunkSize = _uploadClient.ReadInt32(beginResponse, "acceptedChunkSize");
            if (acceptedChunkSize <= 0)
            {
                throw new InvalidOperationException("Device returned an invalid acceptedChunkSize.");
            }

            for (var offset = 0; offset < objectFileBytes.Length; offset += acceptedChunkSize)
            {
                var count = Math.Min(acceptedChunkSize, objectFileBytes.Length - offset);
                var dataRequest = CreateMessage(_uploadClient.CreateDataRequest(uploadId,
                                                                               (uint)offset,
                                                                               objectFileBytes,
                                                                               offset,
                                                                               count,
                                                                               options));
                var ack = WaitForAcceptedResponse(
                    "plcUploadDataRes",
                    options,
                    message => MatchesUploadId(message, uploadId) && MatchesOffset(message, (uint)offset),
                    () => _nodeNetCore.EnqueueOutgoingMessage(dataRequest));
                var expectedOffset = _uploadClient.ReadInt32(ack, "expectedOffset");
                if (expectedOffset != offset + count)
                {
                    throw new InvalidOperationException($"Unexpected expectedOffset in upload ack: {expectedOffset}.");
                }
            }

            var commitResponse = WaitForAcceptedResponse(
                "plcUploadCommitRes",
                WithResponseTimeout(options, GetCommitResponseTimeout(options)),
                message => MatchesUploadId(message, uploadId),
                () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateCommitRequest(uploadId, options))));

            var uploadStatusResponse = ReadDeferredUploadStatus(options, commitResponse);

            return new PlcUploadResult
            {
                UploadId = uploadId,
                BeginResponse = beginResponse,
                CommitResponse = commitResponse,
                UploadStatusResponse = uploadStatusResponse,
            };
        }

        private PlcDownloadResult DownloadBytecode(Transport.ISerialTransport transport, PlcUploadOptions options)
        {
            if (transport == null)
            {
                throw new ArgumentNullException(nameof(transport));
            }

            if (!transport.IsOpen)
            {
                throw new InvalidOperationException("The NodeNet serial transport must be started before downloading a PLC program.");
            }

            using (var stream = new MemoryStream())
            {
                IDictionary<string, object> lastResponse = null;
                uint offset = 0u;
                uint totalSize = 0u;
                bool hasMore;
                do
                {
                    lastResponse = WaitForAcceptedResponse(
                        "plcBytecodeRes",
                        options,
                        message => MatchesSlotId(message, options.SlotId) && MatchesOffset(message, offset),
                        () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateBytecodeReadRequest(options.SlotId,
                                                                                                                          offset,
                                                                                                                          BytecodeReadChunkSize,
                                                                                                                          options))));

                    totalSize = _uploadClient.ReadUInt32(lastResponse, "totalSize");
                    var count = _uploadClient.ReadInt32(lastResponse, "count");
                    var bytes = _uploadClient.ReadBase64Bytes(lastResponse, "dataBase64");
                    if (bytes.Length != count)
                    {
                        throw new InvalidOperationException($"Invalid bytecode chunk length. Expected {count}, got {bytes.Length}.");
                    }

                    if (count > 0)
                    {
                        stream.Write(bytes, 0, bytes.Length);
                    }

                    offset += (uint)count;
                    hasMore = _uploadClient.ReadBoolean(lastResponse, "hasMore");
                }
                while (hasMore);

                if (stream.Length != totalSize)
                {
                    throw new InvalidOperationException($"Incomplete PLC bytecode readback. Expected {totalSize} bytes, got {stream.Length}.");
                }

                return new PlcDownloadResult
                {
                    SlotId = options.SlotId,
                    BytecodeBytes = stream.ToArray(),
                    PayloadBytes = stream.ToArray(),
                    ArtifactType = "linkedBytecodeV1",
                    FinalResponse = lastResponse,
                };
            }
        }

        private PlcDownloadResult DownloadObjectFile(Transport.ISerialTransport transport, PlcUploadOptions options)
        {
            if (transport == null)
            {
                throw new ArgumentNullException(nameof(transport));
            }

            if (!transport.IsOpen)
            {
                throw new InvalidOperationException("The NodeNet serial transport must be started before downloading a PLC object file.");
            }

            using (var stream = new MemoryStream())
            {
                IDictionary<string, object> lastResponse = null;
                uint offset = 0u;
                uint totalSize = 0u;
                bool hasMore;
                do
                {
                    lastResponse = WaitForAcceptedResponse(
                        "plcObjectFileRes",
                        options,
                        message => MatchesSlotId(message, options.SlotId) && MatchesOffset(message, offset),
                        () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateObjectFileReadRequest(options.SlotId,
                                                                                                                            offset,
                                                                                                                            BytecodeReadChunkSize,
                                                                                                                            options))));

                    totalSize = _uploadClient.ReadUInt32(lastResponse, "totalSize");
                    var count = _uploadClient.ReadInt32(lastResponse, "count");
                    var bytes = _uploadClient.ReadBase64Bytes(lastResponse, "dataBase64");
                    if (bytes.Length != count)
                    {
                        throw new InvalidOperationException($"Invalid object-file chunk length. Expected {count}, got {bytes.Length}.");
                    }

                    if (count > 0)
                    {
                        stream.Write(bytes, 0, bytes.Length);
                    }

                    offset += (uint)count;
                    hasMore = _uploadClient.ReadBoolean(lastResponse, "hasMore");
                }
                while (hasMore);

                if (stream.Length != totalSize)
                {
                    throw new InvalidOperationException($"Incomplete PLC object-file readback. Expected {totalSize} bytes, got {stream.Length}.");
                }

                return new PlcDownloadResult
                {
                    SlotId = options.SlotId,
                    PayloadBytes = stream.ToArray(),
                    ArtifactType = _uploadClient.ReadString(lastResponse, "artifactType"),
                    FinalResponse = lastResponse,
                };
            }
        }

        private IDictionary<string, object> WaitForAcceptedResponse(
            string expectedCommand,
            PlcUploadOptions options,
            Func<NodeNetMessage, bool> extraPredicate,
            Action sendAction)
        {
            var responseMessage = _nodeNetCore.ExecuteWithIncomingMessageWait(
                message => IsExpectedResponse(message, expectedCommand, options) && extraPredicate(message),
                options.ResponseTimeout,
                sendAction);
            return _uploadClient.ExpectOk(responseMessage.ToDictionary(), expectedCommand);
        }

        private IDictionary<string, object> ReadDeferredUploadStatus(PlcUploadOptions options,
                                                                     IDictionary<string, object> commitResponse)
        {
            if (!_uploadClient.ReadBoolean(commitResponse, "autoLoadPending"))
            {
                return null;
            }

            var statusOptions = WithResponseTimeout(options, DeferredStatusResponseTimeout);
            var deadline = DateTime.UtcNow + DeferredStatusObservationWindow;
            IDictionary<string, object> lastResponse = null;

            while (DateTime.UtcNow < deadline)
            {
                try
                {
                    lastResponse = WaitForAcceptedResponse(
                        "plcUploadStatusRes",
                        statusOptions,
                        message => true,
                        () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateStatusRequest(statusOptions))));
                }
                catch
                {
                    continue;
                }

                if (HasDeferredUploadResult(lastResponse, options.SlotId))
                {
                    break;
                }
            }

            return lastResponse;
        }

        private static TimeSpan GetCommitResponseTimeout(PlcUploadOptions options)
        {
            if (options == null)
            {
                throw new ArgumentNullException(nameof(options));
            }

            return options.PersistToFlash && options.ResponseTimeout < PersistedCommitResponseTimeout
                ? PersistedCommitResponseTimeout
                : options.ResponseTimeout;
        }

        private static PlcUploadOptions WithResponseTimeout(PlcUploadOptions source, TimeSpan responseTimeout)
        {
            if (source == null)
            {
                throw new ArgumentNullException(nameof(source));
            }

            return new PlcUploadOptions
            {
                LocalAddress = source.LocalAddress,
                RemoteAddress = source.RemoteAddress,
                SlotId = source.SlotId,
                PersistToFlash = source.PersistToFlash,
                AutoLoad = source.AutoLoad,
                ResponseTimeout = responseTimeout,
                ObjectFileOptions = source.ObjectFileOptions,
            };
        }

        private static bool IsExpectedResponse(NodeNetMessage message, string expectedCommand, PlcUploadOptions options)
        {
            return message != null &&
                   string.Equals(message.Command, expectedCommand, StringComparison.Ordinal) &&
                   message.From == options.RemoteAddress &&
                   message.To == options.LocalAddress;
        }

        private static bool MatchesUploadId(NodeNetMessage message, uint uploadId)
        {
            return TryReadUInt32(message, "uploadId", out var messageUploadId) && messageUploadId == uploadId;
        }

        private static bool MatchesOffset(NodeNetMessage message, uint offset)
        {
            return TryReadUInt32(message, "offset", out var messageOffset) && messageOffset == offset;
        }

        private static bool MatchesSlotId(NodeNetMessage message, ushort slotId)
        {
            return TryReadUInt32(message, "slotId", out var messageSlotId) && messageSlotId == slotId;
        }

        private static bool HasDeferredUploadResult(IDictionary<string, object> response, ushort slotId)
        {
            if (response == null)
            {
                return false;
            }

            if (!response.TryGetValue("lastAutoLoadValid", out var validValue) || validValue == null || !Convert.ToBoolean(validValue))
            {
                return false;
            }

            if (!response.TryGetValue("lastAutoLoadSlotId", out var slotValue) || slotValue == null)
            {
                return false;
            }

            try
            {
                return Convert.ToUInt16(slotValue) == slotId;
            }
            catch
            {
                return false;
            }
        }

        private static bool TryReadUInt32(NodeNetMessage message, string key, out uint value)
        {
            value = 0u;
            if (message == null || !message.TryGetExtensionValue(key, out var rawValue) || rawValue == null)
            {
                return false;
            }

            try
            {
                value = Convert.ToUInt32(rawValue);
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static NodeNetMessage CreateMessage(IDictionary<string, object> payload)
        {
            if (payload == null)
            {
                throw new ArgumentNullException(nameof(payload));
            }

            var message = new NodeNetMessage();
            foreach (var entry in payload)
            {
                switch (entry.Key)
                {
                    case "cmd":
                        message.Command = Convert.ToString(entry.Value);
                        break;
                    case "from":
                        message.From = Convert.ToByte(entry.Value);
                        break;
                    case "to":
                        message.To = Convert.ToByte(entry.Value);
                        break;
                    default:
                        message.SetExtensionValue(entry.Key, entry.Value);
                        break;
                }
            }

            return message;
        }
    }
}