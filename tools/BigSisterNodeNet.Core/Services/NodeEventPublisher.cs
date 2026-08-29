using BigSisterNodeNet.Core.Models;
using System;

namespace BigSisterNodeNet.Core.Services
{
    public class NodeEventPublisher : INodeEventPublisher
    {
        private readonly Action<INode> _nodeUpdatedCallback;
        private readonly Action<INode> _nodeHeartbeatCallback;

        public NodeEventPublisher(Action<INode> nodeUpdatedCallback, Action<INode> nodeHeartbeatCallback)
        {
            _nodeUpdatedCallback = nodeUpdatedCallback;
            _nodeHeartbeatCallback = nodeHeartbeatCallback;
        }

        public void PublishNodeUpdated(INode node)
        {
            _nodeUpdatedCallback?.Invoke(node);
        }

        public void PublishNodeHeartbeat(INode node)
        {
            _nodeHeartbeatCallback?.Invoke(node);
        }
    }
}
