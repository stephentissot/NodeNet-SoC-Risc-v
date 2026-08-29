using BigSisterNodeNet.Core.Models;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core
{
    public partial class NodeNetMessage 
    {

        [JsonProperty("wifi")]
        public WifiSettings? Wifi{ get; set; }

        [JsonProperty("mqtt")]
        public MqttSettings? Mqtt { get; set; }
    }


    public class WifiSettings : NodePropertyHandler
    {

        
        [JsonProperty("enabled")]
        public bool Enabled { get; set; }

        [JsonProperty("connected")]
        public bool Connected { get; set; }
        
        [JsonProperty("ssid")]
        public string WifiSSID { get; set; }
        
        [JsonProperty("password")]  
        public string Password { get; set; }
        
        [JsonProperty("lastError")]
        public string LastError { get; set;  }

    }

    public class MqttSettings : NodePropertyHandler
    {        
        [JsonProperty("enabled")]
        public bool Enabled { get; set; }

        [JsonProperty("connected")]
        public bool Connected { get; set; }

        [JsonProperty("ipAddress")]
        public string IpAddress { get; set; } // Broker ip address

        [JsonProperty("portNumber")]
        public int Port { get; set; } // Broker port number
        
        [JsonProperty("login")]
        public string Login { get; set; }

        [JsonProperty("password")]
        public string Password { get; set; } // Will be "********" if set in the UI, otherwise it will be empty string if not set. The actual password is not sent back to the UI for security reasons.

        [JsonProperty("lastError")]
        public string LastError { get; set; }


    }
}
