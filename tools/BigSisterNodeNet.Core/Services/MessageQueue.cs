using System.Collections.Concurrent;

namespace BigSisterNodeNet.Core.Services
{
    public class MessageQueue : IMessageQueue
    {
        private readonly BlockingCollection<NodeNetMessage> _outgoing;

        public MessageQueue(BlockingCollection<NodeNetMessage> outgoing)
        {
            _outgoing = outgoing;
        }

        public void Enqueue(NodeNetMessage message)
        {
            _outgoing.Add(message);
        }
    }
}
