using BigSisterNodeNet.Core.Models;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;

namespace BigSisterNodeNet.Core.Services
{
    public class NodeRepository : INodeRepository
    {
        private readonly BlockingCollection<INode> _nodeList;

        public NodeRepository(BlockingCollection<INode> nodeList)
        {
            _nodeList = nodeList;
        }

        public void Add(INode node)
        {
            _nodeList.Add(node);
        }

        public INode Get(string deviceId)
        {
            return _nodeList.FirstOrDefault(n => n.DeviceId == deviceId);
        }

        public void Remove(string deviceId)
        {
            var node = _nodeList.FirstOrDefault(n => n.DeviceId == deviceId);
            if (node != null)
            {
                _nodeList.TryTake(out node);
            }
        }

        public INode GetByAddress(int address)
        {
            return _nodeList.FirstOrDefault(n => n.Address == address);
        }

        public bool Contains(string deviceId)
        {
            return _nodeList.Any(n => n.DeviceId == deviceId);
        }

        public IEnumerable<INode> GetAll()
        {
            return _nodeList.ToList();
        }
    }
}
