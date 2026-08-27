using BigSisterNodeNet.Core.Extensions;
using BigSisterNodeNet.Core.Services;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Models
{
    public class Node : NodePropertyHandler, INode
    {

        /// <summary>
        /// Envoie une mise à jour de propriété au réseau NodeNet en utilisant le nom JSON de la propriété
        /// Vérifie que la valeur a changé avant d'envoyer la mise à jour
        /// </summary>
        /// <typeparam name="T">Type de la propriété</typeparam>
        /// <param name="propertyExpression">Expression lambda pointant vers la propriété</param>
        /// <param name="value">Nouvelle valeur</param>
        protected void SendPropertyUpdate<T>(byte Address, System.Linq.Expressions.Expression<Func<INode, T>> propertyExpression, T value)
        {
            // Utilise le service injecté ou le service locator (pour les objets désérialisés)
            //var service = _nodeUpdateService ?? NodeUpdateServiceLocator.Current;

            if (_nodeUpdateService != null)
            {
                var jsonPropertyName = this.GetJsonPropertyName(propertyExpression);
                _nodeUpdateService.UpdateNode(this, Address, jsonPropertyName, value);
            }
        }

        public Node()
        {
            Features = new Dictionary<string, bool>();            
        }
        public Node(NodeNetMessage message) : this()
        {
            Update(message);
        }

        


        public void Reboot()
        {
            var message = new NodeNetMessage()
            {
                Command = NodeNetCommands.Reboot,
                From = NodeNetAddress.SerialEndpoint,
                To = Address
            };
            _nodeUpdateService.SendCommand(message);
            IsOnline = false;
        }

        public void RequestNetworkSettings()
        {
            var message = new NodeNetMessage()
            {
                Command = NodeNetCommands.Network_Req,
                From = NodeNetAddress.SerialEndpoint,
                To = Address
            };
            _nodeUpdateService.SendCommand(message);
        }

        public void UpdateNetworkSettings(bool enabled, string wifiSSID, string password)
        {
            var message = new NodeNetMessage()
            {
                Command = NodeNetCommands.Network_Upd,
                From = NodeNetAddress.SerialEndpoint,
                To = Address,
                Wifi = new WifiSettings() { Enabled = enabled, WifiSSID = wifiSSID, Password = password },
            };
            _nodeUpdateService.SendCommand(message);
        }

        public void UpdateMqttSettings(bool enabled, string ipAddress, int port, string login, string password)
        {
            var message = new NodeNetMessage()
            {
                Command = NodeNetCommands.Network_Upd,
                From = NodeNetAddress.SerialEndpoint,
                To = Address,
                Mqtt = new MqttSettings() { Enabled = enabled, IpAddress = ipAddress, Port = port, Login = login, Password = password },
            };
            _nodeUpdateService.SendCommand(message);
        }

        public virtual void Update(NodeNetMessage message)
        {
            if (message != null)
            {
                Address = message.From;
                DeviceId = message.DeviceId ?? DeviceId;
                Master = message.Master.HasValue ? message.Master.Value : Master;
                InstrumentName = message.InstrumentName ?? InstrumentName;
                HardwareType = message.HardwareType.HasValue ? message.HardwareType.Value : HardwareType;
                Status = message.Status.HasValue ? message.Status.Value : Status;
                Rs485Terminator = message.Rs485Terminator.HasValue ? message.Rs485Terminator.Value : Rs485Terminator;
                Temperature = message.Temperature.HasValue ? message.Temperature.Value : Temperature;
                Features = message.Features ?? Features;
                IsOnline = true;
                LastSeen = DateTime.Now;
                // Network settings update
                if (message.Wifi != null)
                {
                    if(Wifi == null)
                    {
                        Wifi = new WifiSettings();
                    }
                    Wifi.Enabled = message.Wifi.Enabled;
                    Wifi.WifiSSID = message.Wifi.WifiSSID;
                    Wifi.Password = message.Wifi.Password;
                    Wifi.LastError = message.Wifi.LastError;
                }
                // MQTT settings update
                if (message.Mqtt != null)
                {
                    if(Mqtt == null)
                    {
                        Mqtt = new MqttSettings();
                    }
                    Mqtt.Enabled = message.Mqtt.Enabled;
                    Mqtt.IpAddress = message.Mqtt.IpAddress;
                    Mqtt.Port = message.Mqtt.Port;
                    Mqtt.Login = message.Mqtt.Login;
                    Mqtt.Password = message.Mqtt.Password;
                    Mqtt.LastError = message.Mqtt.LastError;
                }
            }
        }


        private byte _address;
        [JsonProperty("address")]
        public byte Address { get => _address; set => SetProperty(ref _address, value); }

        private string _deviceId;
        [JsonProperty("deviceId")]

        private bool _master;
        [JsonProperty("master")]
        public bool Master { get => _master; set => SetProperty(ref _master, value); }

        public string DeviceId { get => _deviceId.ToString(); set => SetProperty(ref _deviceId, value); }

        private string _instrumentName;
        [JsonProperty("instrumentName")]
        public string InstrumentName
        {
            get => _instrumentName?.ToString();
            set
            {
                var Address = this.Address;
                if (SetProperty(ref _instrumentName, value))
                {
                    // Envoie la mise à jour au réseau avec le nom JSON correct
                    SendPropertyUpdate(Address, x => x.InstrumentName, value);
                }
            }
        }

        private HardwareType _hardwareType;
        [JsonProperty("hardwareType")]
        public HardwareType HardwareType { get => _hardwareType; set => SetProperty(ref _hardwareType, value); }

        private Status _status;
        [JsonProperty("status")]
        public Status Status { get => _status; set => SetProperty(ref _status, value); }

        private bool _rs485Terminator;
        [JsonProperty("rs485Terminator")]
        public bool Rs485Terminator
        {
            get => _rs485Terminator;
            set
            {
                if (SetProperty(ref _rs485Terminator, value))
                {
                    SendPropertyUpdate(Address, x => x.Rs485Terminator, value);
                }
            }
        }

        private float _temperature;
        [JsonProperty("temperature")]
        public float Temperature { get => _temperature; set => SetProperty(ref _temperature, value); }

        private Dictionary<string, bool> _features;
        [JsonProperty("features")]

        private bool _isOnline;
        public bool IsOnline { get => _isOnline; set => SetProperty(ref _isOnline, value); }

        private DateTime _lastSeen;
        public DateTime LastSeen { get => _lastSeen; set => SetProperty(ref _lastSeen, value); }

        public Dictionary<string, bool> Features { get => _features; set => SetProperty(ref _features, value); }

        private WifiSettings _wifi;
        public WifiSettings? Wifi { get => _wifi; set => SetProperty(ref _wifi, value); }

        private MqttSettings _mqttSettings;
        public MqttSettings? Mqtt { get => _mqttSettings; set => SetProperty(ref _mqttSettings, value); }

    }


}
