using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.Core.Services;
using Serilog.Data;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace BigSisterNodeNet.Core.HandleCommands
{
    public class Handler
    {
        protected readonly INodeRepository _nodeRepository;
        protected readonly IMessageQueue _messageQueue;
        protected readonly INodeEventPublisher _eventPublisher;
        protected readonly INodeUpdateService _nodeUpdateService;

        public Handler(
            INodeRepository nodeRepository,
            IMessageQueue messageQueue,
            INodeEventPublisher eventPublisher,
            INodeUpdateService nodeUpdateService)
        {
            _nodeRepository = nodeRepository;
            _messageQueue = messageQueue;
            _eventPublisher = eventPublisher;
            _nodeUpdateService = nodeUpdateService;
        }

        public void UpdateNodes(NodeNetMessage message)
        {
            if (_nodeRepository.Contains(message.DeviceId))
            {
                var node = _nodeRepository.Get(message.DeviceId);
                node.Update(message);
                _eventPublisher.PublishNodeUpdated(node);
            }
            else
            {
                switch (message.HardwareType)
                {
                    case HardwareType.FilterWheel:
                        var newFilterNode = new FilterNode(message);
                        _nodeRepository.Add(newFilterNode);
                        _eventPublisher.PublishNodeUpdated(newFilterNode);
                        break;
                    case HardwareType.Focuser:
                        var newFocuserNode = new FocuserNode(message);
                        _nodeRepository.Add(newFocuserNode);
                        _eventPublisher.PublishNodeUpdated(newFocuserNode);
                        break;
                    case HardwareType.NodeNet_SOC:
                        var newSoCNode = new NodeNet_SOC(message);
                        _nodeRepository.Add(newSoCNode);
                        _eventPublisher.PublishNodeUpdated(newSoCNode);
                        break;
                    default:

                        break;
                }
            }

        }

        public void NodePulse(NodeNetMessage message)
        {
            var node = _nodeRepository.GetByAddress(message.From);
            if (node != null)
            {
                _eventPublisher.PublishNodeHeartbeat(node);
                node.IsOnline = true;
                node.LastSeen = DateTime.Now;
            }
            var masterNode = _nodeRepository.GetAll().FirstOrDefault(x => x.Master);
            if(masterNode != null)
            {
                masterNode.IsOnline = true;
                masterNode.LastSeen = DateTime.Now;
            }
        }

    }
}
