using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.PlcCore;
using System;
using System.Collections.Generic;
using System.Linq;

namespace BigSisterNodeNet.Core.Services
{
    public class PointCatalogService : IPointCatalogService
    {
        private const int DefaultPageSize = 100;

        private readonly INodeRepository _nodeRepository;
        private readonly IMessageQueue _messageQueue;
        private readonly INodeEventPublisher _eventPublisher;
        private readonly Dictionary<string, string> _pendingPointDefinitionRequests = new Dictionary<string, string>(StringComparer.Ordinal);
        private readonly Dictionary<string, string> _pendingPointStateRequests = new Dictionary<string, string>(StringComparer.Ordinal);

        public PointCatalogService(
            INodeRepository nodeRepository,
            IMessageQueue messageQueue,
            INodeEventPublisher eventPublisher)
        {
            _nodeRepository = nodeRepository;
            _messageQueue = messageQueue;
            _eventPublisher = eventPublisher;
        }

        public void RequestPointDefinitions(NodeNet_SOC node, string path = "", int offset = 0, int limit = DefaultPageSize)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            if (offset <= 0)
            {
                node.MarkPointDefinitionPathRequested(path);
                _pendingPointDefinitionRequests[NormalizePath(path)] = node.DeviceId;
            }

            _messageQueue.Enqueue(new PointDefinitionsRequest
            {
                From = NodeNetAddress.SerialEndpoint,
                To = node.Address,
                Path = path ?? string.Empty,
                Offset = Math.Max(0, offset),
                Limit = NormalizeLimit(limit)
            });
        }

        public void RequestPointStates(NodeNet_SOC node, string path = "", int offset = 0, int limit = DefaultPageSize)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            if (offset <= 0)
            {
                _pendingPointStateRequests[NormalizePath(path)] = node.DeviceId;
            }

            _messageQueue.Enqueue(new PointStatesRequest
            {
                From = NodeNetAddress.SerialEndpoint,
                To = node.Address,
                Path = path ?? string.Empty,
                Offset = Math.Max(0, offset),
                Limit = NormalizeLimit(limit)
            });
        }

        public void UpsertPointDefinition(NodeNet_SOC node, PointDefinition definition)
        {
            if (node == null)
            {
                throw new ArgumentNullException(nameof(node));
            }

            if (definition == null)
            {
                throw new ArgumentNullException(nameof(definition));
            }

            _messageQueue.Enqueue(new PointUpsertRequest
            {
                From = NodeNetAddress.SerialEndpoint,
                To = node.Address,
                Definition = definition
            });
        }

        public void HandlePointDefinitionsResponse(PointDefinitionsResponse response)
        {
            if (response == null)
            {
                return;
            }

            var node = ResolveNode(response.From, response.DeviceId, response.Path, _pendingPointDefinitionRequests);
            if (node == null)
            {
                return;
            }

            node.ApplyPointDefinitionsResponse(response);
            _eventPublisher.PublishNodeUpdated(node);

            EnqueueNextDefinitionsPage(node, response);
        }

        public void HandlePointStatesResponse(PointStatesResponse response)
        {
            if (response == null)
            {
                return;
            }

            var node = ResolveNode(response.From, response.DeviceId, response.Path, _pendingPointStateRequests);
            if (node == null)
            {
                return;
            }

            node.ApplyPointStatesResponse(response);
            _eventPublisher.PublishNodeUpdated(node);

            EnqueueNextStatesPage(node, response);
        }

        private NodeNet_SOC ResolveNode(byte nodeAddress, string deviceId, string path, IDictionary<string, string> pendingRequests)
        {
            var node = _nodeRepository.GetByAddress(nodeAddress) as NodeNet_SOC;
            if (node != null)
            {
                return node;
            }

            if (!string.IsNullOrWhiteSpace(deviceId))
            {
                return _nodeRepository.Get(deviceId) as NodeNet_SOC;
            }

            if (pendingRequests != null && pendingRequests.TryGetValue(NormalizePath(path), out var pendingDeviceId) && !string.IsNullOrWhiteSpace(pendingDeviceId))
            {
                return _nodeRepository.Get(pendingDeviceId) as NodeNet_SOC;
            }

            return null;
        }

        private void EnqueueNextDefinitionsPage(NodeNet_SOC node, PointDefinitionsResponse response)
        {
            if (!response.HasMore)
            {
                return;
            }

            var actualCount = response.Points?.Count ?? 0;
            if (actualCount <= 0)
            {
                actualCount = response.Definitions?.Count ?? 0;
            }

            var pageSize = GetPageSize(response.Count, actualCount);
            if (pageSize <= 0)
            {
                return;
            }

            RequestPointDefinitions(node, response.Path, response.Offset + pageSize, response.Limit > 0 ? response.Limit : pageSize);
        }

        private void EnqueueNextStatesPage(NodeNet_SOC node, PointStatesResponse response)
        {
            if (!response.HasMore)
            {
                return;
            }

            var actualCount = response.PointStates?.Count ?? 0;
            if (actualCount <= 0)
            {
                actualCount = response.States?.Count ?? 0;
            }

            var pageSize = GetPageSize(response.Count, actualCount);
            if (pageSize <= 0)
            {
                return;
            }

            RequestPointStates(node, response.Path, response.Offset + pageSize, response.Limit > 0 ? response.Limit : pageSize);
        }

        private static int GetPageSize(int count, int actualCount)
        {
            if (count > 0)
            {
                return count;
            }

            return actualCount;
        }

        private static int NormalizeLimit(int limit)
        {
            return limit > 0 ? limit : DefaultPageSize;
        }

        private static string NormalizePath(string path)
        {
            return path?.Trim() ?? string.Empty;
        }
    }
}
