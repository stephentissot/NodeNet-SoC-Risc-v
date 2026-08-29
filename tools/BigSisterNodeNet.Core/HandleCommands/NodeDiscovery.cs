using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.Core.Services;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.HandleCommands
{
    public class NodeDiscovery : Handler
    {

        public NodeDiscovery(
            INodeRepository nodeRepository,
            IMessageQueue messageQueue,
            INodeEventPublisher eventPublisher,
            INodeUpdateService nodeUpdateService)
            : base(nodeRepository, messageQueue, eventPublisher, nodeUpdateService) 
        {
            // Create a new instance of the NodeDiscovery class with the specified dependencies.
        }



        public void WhoIs()
        {
            var message = NodeData();
            message.Command = NodeNetCommands.WhoIs;
            _messageQueue.Enqueue(message);
        }
        public void IAm()
        {            
            var message = NodeData();
            message.Command = NodeNetCommands.IAm;
            _messageQueue.Enqueue(message);
        }
        private NodeNetMessage NodeData()
        {
            var message = new NodeNetMessage
            {
                From = NodeNetAddress.SerialEndpoint,
                To = NodeNetAddress.Broadcast,
                DeviceId = "WinDriver",
                HardwareType = HardwareType.AscomBridge,
                InstrumentName = Environment.MachineName,
                Master = false,
                Status = NodeNetCore.Status,
            };
            return message;
        }

        public void OnNodePulse(NodeNetMessage message)
        {
            base.NodePulse(message);
        }

        public void Heartbeat()
        {
            var message = new NodeNetMessage();
            message.From = NodeNetAddress.SerialEndpoint;
            message.To = NodeNetAddress.Broadcast;
            message.Command = NodeNetCommands.HeartBeat;
            //_messageQueue.Enqueue(message);
        }

        public void CheckNodeHeartbeats() {
            // Check node last seen and set IsOnline to false if last seen is more than 30 seconds ago
            foreach (var node in _nodeRepository.GetAll())
            {
                if (node.IsOnline && (DateTime.Now - node.LastSeen).TotalSeconds > 30)
                {
                    node.IsOnline = false;
                    _eventPublisher.PublishNodeUpdated(node);
                }
            }

        }
    }
}
