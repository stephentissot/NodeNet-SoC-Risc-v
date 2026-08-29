using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core
{
    static public class NodeNetCommands
    {
        public const string WhoIs = "WhoIs";
        public const string IAm = "IAm";
        public const string HeartBeat = "heartbeat";
        public const string RemoteResponseData = "remote.responseData";
        public const string Reboot = "reboot";

        // Network/mqtt
        public const string Network_Req = "networkReq";
        public const string Network_Res = "networkRes";
        public const string Network_Upd = "networkUpd";

        // PLC / NodeNet SoC
        public const string FeaturesReq = "FeaturesReq";
        public const string FeaturesRes = "FeaturesRes";
        public const string PointDefinitionsReq = "pointDefsReq";
        public const string PointDefinitionsRes = "pointDefsRes";
        public const string PointStatesReq = "pointStatesReq";
        public const string PointStatesRes = "pointStatesRes";
        public const string PointUpsert = "pointUpsert";
        public const string PointDelete = "pointDelete";

        // Filter
        public const string GoToPosition = "filter.Go";

        //static public string MoveUp = ">";
        //static public string MoveDown = "<";

        //// Actions with filterNo (example "H2" "M4,200", "N2,0,MyName")
        
        //static public string TestHomingOffset = "H";
        //static public string SetMotorOffset = "M";
        //static public string SetFocusOffset = "F";
        //static public string SetFilterName = "N";


        //// Infos
        //static public string Ping = "I0";
        //static public string GetFilterCount = "I1";
        //static public string Position = "I2";
        //static public string GetSpeed = "I3";
        //static public string GetAccellSpeed = "I4";
        //static public string GetHomingOffset = "I5";
        //static public string FilterNames = "I7";
        //static public string MotorOffsets = "I8";
        //static public string FocuserOffsets = "I9";
        //// Actions
        
        //static public string SetFilterCount = "R1";
        //static public string SetHomingOffset = "R2";
        //static public string Setpeed = "R3";
        //static public string SetAccellSpeed = "R4";
    }

}
