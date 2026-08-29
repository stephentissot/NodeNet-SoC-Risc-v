using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.HandleCommands;
using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.UI.Models.Settings;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Material.Icons;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics.Eventing.Reader;
using System.Linq;
using System.Runtime.Remoting.Contexts;
using System.Windows.Controls;
using System.Windows.Input;
namespace BigSisterNodeNet.UI.Models
{
    public partial class NodeViewModel : ObservableObject, ITabViewModel
    {
        public INode _node;
        public INode Node => _node;


        public bool HasFilterSettings 
        { 
            get 
            {
                return Node is FilterNode;
            } 
        }

        public bool HasNetworkSettings 
        {
            get
            {
                if (Node.Features == null) return false;
                return Node.Features.ContainsKey("hasWifi") || Node.Features.ContainsKey("hasW5500Driver");
            }
        }

        public bool HasMqttSettings
        {
            get
            {
                if (Node.Features == null) return false;
                return Node.Features.ContainsKey("hasMqtt");
            }
        }

        public bool HasProgrammingTab
        {
            get
            {
                return Node is NodeNet_SOC;
            }
        }

        public NetworkSettingsViewModel Network { get; }
        public MqttSettingsViewModel Mqtt { get; }

        public bool IsEnabled { get { return _node.IsOnline; } }

        [ObservableProperty]
        public partial bool IsVisible { get; set; }

        public string DeviceId { get { return _node.DeviceId; } }

        [ObservableProperty]
        public partial bool Heartbeat { get; set; }

        [ObservableProperty]
        public partial bool MasterHeartbeat { get; set; }

        public ICommand RebootCommand { get; }

        public ObservableCollection<FeatureIcon> Features
        {
            get
            {
                var features = new ObservableCollection<FeatureIcon>();
                if (_node.Features != null)
                {
                    foreach (var feature in _node.Features.Where(x => x.Value))
                    {
                        if (FeatureIcons.Map.TryGetValue(feature.Key, out var iconName))
                        {
                            var icon = new FeatureIcon(feature.Key, iconName, feature.Value);
                            features.Add(icon);
                        }
                    }
                } return features;
            }
            private set;
        } = new ObservableCollection<FeatureIcon>();


        private bool _isNetworkTabSelected;
        public bool IsNetworkTabSelected
        {
            get => _isNetworkTabSelected;
            set
            {
                if (SetProperty(ref _isNetworkTabSelected, value) && value)
                {
                    Node.RequestNetworkSettings();
                }
            }
        }
        public MaterialIconKind? MasterIcon { 
            get
            {
                if (_node.Master) return MaterialIconKind.Usb;
                else return null;

            } 
        }

        public void OnSelected() // Raised when this node tab is selected in the UI.
        {

        }

        public NodeViewModel(INode node)
        {
            _node = node;
            RebootCommand = new RelayCommand(RebootNode);
            Network = new NetworkSettingsViewModel(node);
            Mqtt = new MqttSettingsViewModel(node);
        }

        public virtual void Update(INode node)
        {
            _node = node;
            OnPropertyChanged(nameof(Node));
            OnPropertyChanged(nameof(IsEnabled));
            OnPropertyChanged(nameof(Features));
            OnPropertyChanged(nameof(HasNetworkSettings));
            OnPropertyChanged(nameof(HasMqttSettings));
            OnPropertyChanged(nameof(HasProgrammingTab));
            OnPropertyChanged(nameof(MasterIcon));
            if(_node.Wifi != null)
            {
                Network.Update(_node.Wifi);
            }
            if(_node.Mqtt != null)
            {
                Mqtt.Update(_node.Mqtt);
            }
        }

        private void RebootNode()
        {
            Node.Reboot();
        }
    }    
    

    public class FeatureIcon
    {
        public string Tooltip { get; private set; }
        public MaterialIconKind Icon { get; private set; }
        public bool Enabled { get; private set; }


        public FeatureIcon(string tooltip, MaterialIconKind icon, bool enabled)
        {
            Tooltip = tooltip;
            Icon = icon;
            Enabled = enabled;
        }
    }
    public static class FeatureIcons
    {
        public static readonly Dictionary<string, MaterialIconKind> Map =
            new()
            {
            { "hasTemperatureSensor", MaterialIconKind.Temperature },
            { "hasTMC2209Driver", MaterialIconKind.Chip },
            { "hasWifi", MaterialIconKind.Wifi },
            { "hasW5500Driver", MaterialIconKind.Ethernet },
            { "hasMqtt", MaterialIconKind.LanConnect },
            { "hasOledDisplay", MaterialIconKind.Monitor },
            };
    }
}
