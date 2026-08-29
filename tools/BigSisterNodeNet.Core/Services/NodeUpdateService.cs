using BigSisterNodeNet.Core.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Services
{
    public class NodeUpdateService : INodeUpdateService
    {
        protected readonly INodeRepository _nodeRepository;
        protected readonly IMessageQueue _messageQueue;
        protected readonly INodeEventPublisher _eventPublisher;

        public NodeUpdateService(
            INodeRepository nodeRepository,
            IMessageQueue messageQueue,
            INodeEventPublisher eventPublisher)
        {
            _nodeRepository = nodeRepository;
            _messageQueue = messageQueue;
            _eventPublisher = eventPublisher;
        }
        /// <summary>
        /// Envoie une commande de mise à jour d'une propriété à un nœud du réseau NodeNet
        /// </summary>
        /// <param name="node">Le nœud cible</param>
        /// <param name="propertyName">Le nom de la propriété à mettre à jour</param>
        /// <param name="value">La nouvelle valeur de la propriété</param>
        public void UpdateNode(INode node, byte Address, string propertyName, object value)
        {
            var message = new NodeNetMessage
            {
                Command = "updateProperty",
                From = NodeNetAddress.SerialEndpoint,
                To = Address,
                PropertyName = propertyName,
                Value = value
            };
            _messageQueue.Enqueue(message);
        }

        public void SendCommand(NodeNetMessage message)
        {
            _messageQueue.Enqueue(message);
        }
    }
}
