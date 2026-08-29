using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Models
{
    public enum HardwareType : byte
    {
        FilterWheel = 0,
        Focuser = 1,
        Rotator = 2,
        IO8 = 3,
        IO16 = 4,
        AscomBridge = 5,
        NodeNet_SOC = 6,
        Undefinded
    }

    public enum Status : byte
    {
        INIT = 0,
        HOMING_MAGNETOFF = 1,
        HOMING = 2,
        MOVING = 3,
        READY = 4,
        HOMINGERROR = 5,
        ERROR
    };
}
