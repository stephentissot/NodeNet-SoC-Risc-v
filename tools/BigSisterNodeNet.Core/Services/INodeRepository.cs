using BigSisterNodeNet.Core.Models;
using System.Collections.Generic;
using System.Linq;

namespace BigSisterNodeNet.Core.Services
{
    public interface INodeRepository
    {
        void Add(INode node);
        INode Get(string deviceId);
        INode GetByAddress(int address);
        bool Contains(string deviceId);
        void Remove(string deviceId);

        IEnumerable<INode> GetAll();
    }
}
