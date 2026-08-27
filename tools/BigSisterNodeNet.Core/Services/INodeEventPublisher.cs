using BigSisterNodeNet.Core.Models;

namespace BigSisterNodeNet.Core.Services
{
    public interface INodeEventPublisher
    {
        void PublishNodeUpdated(INode node);
        void PublishNodeHeartbeat(INode node);
    }
}
