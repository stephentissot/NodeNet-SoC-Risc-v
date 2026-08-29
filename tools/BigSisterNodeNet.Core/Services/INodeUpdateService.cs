using BigSisterNodeNet.Core.Models;

namespace BigSisterNodeNet.Core.Services
{
    public interface INodeUpdateService
    {
        void UpdateNode(INode node, byte Address, string propertyName, object value);
        void SendCommand(NodeNetMessage message);
    }
}