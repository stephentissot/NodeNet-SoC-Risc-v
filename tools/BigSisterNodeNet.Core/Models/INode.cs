using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Models
{
    public interface INode : INotifyPropertyChanged
    {
        void Update(NodeNetMessage message);

        void Reboot();

        void RequestNetworkSettings();

        void UpdateNetworkSettings(bool enabled, string wifiSSID, string password);

        void UpdateMqttSettings(bool enabled, string ipAddress, int port, string login, string password);

        byte Address { get; set; }
        string DeviceId { get; set; }
        string InstrumentName { get; set; }

        bool IsOnline { get; set; }

        bool Master { get; set; }
        HardwareType HardwareType { get; set; }
        Status Status { get; set; }
        bool Rs485Terminator { get; set; }
        float Temperature { get; set; }

        DateTime LastSeen { get; set; }
        Dictionary<string, bool> Features { get; set; }

        WifiSettings? Wifi { get; set; }

        MqttSettings? Mqtt { get; set; }

    }
}
