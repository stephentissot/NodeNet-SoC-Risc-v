using BigSisterNodeNet.Core.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Instruments
{
    public class RotatorNode : Node
    {
        public RotatorNode(NodeNetMessage message) : base(message)
        {
            Update(message);
        }
    }
}
