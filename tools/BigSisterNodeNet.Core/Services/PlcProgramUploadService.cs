using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Plc;
using System;
using System.Collections.Generic;

namespace BigSisterNodeNet.Core.Services
{
    public class PlcProgramUploadService : IPlcProgramUploadService
    {
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
                ObjectFileOptions = objectFileOptions ?? new PlcObjectFileOptions(),
            };

            var objectFileBytes = _uploadClient.BuildObjectFile(program, options);
            return _nodeNetCore.ExecuteExclusiveTransportSession(transport => UploadObjectFile(transport, objectFileBytes, options));
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
                options,
                message => MatchesUploadId(message, uploadId),
                () => _nodeNetCore.EnqueueOutgoingMessage(CreateMessage(_uploadClient.CreateCommitRequest(uploadId, options))));

            return new PlcUploadResult
            {
                UploadId = uploadId,
                BeginResponse = beginResponse,
                CommitResponse = commitResponse,
            };
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