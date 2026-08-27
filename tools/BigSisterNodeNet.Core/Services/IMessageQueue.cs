namespace BigSisterNodeNet.Core.Services
{
    public interface IMessageQueue
    {
        void Enqueue(NodeNetMessage message);
    }
}
