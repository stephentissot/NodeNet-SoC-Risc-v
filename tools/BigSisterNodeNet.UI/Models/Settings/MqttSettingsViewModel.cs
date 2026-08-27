using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace BigSisterNodeNet.UI.Models.Settings
{
    public partial class MqttSettingsViewModel : ObservableObject
    {
        private INode _node;

        public bool IsUpdated
        {
            get
            {
                if(_node.Mqtt != null)
                {
                    if (Enabled != _node.Mqtt.Enabled) return true;
                    if (IpAddress != _node.Mqtt.IpAddress) return true;
                    if (PortNumber != _node.Mqtt.Port) return true;
                    if (Login != _node.Mqtt.Login) return true;
                    if (Password != _node.Mqtt.Password) return true;
                }
                
                return false;
            }
        }

        [ObservableProperty]
        private bool isSaving;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private bool enabled;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string ipAddress;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private int portNumber;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string login;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string password;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string lastError;

        public ICommand GoSave { get; }

        public MqttSettingsViewModel(INode node)
        {
            _node = node;
            
            GoSave = new RelayCommand(Save);
        }

        public void Update(MqttSettings mqtt)
        {
            if (mqtt == null) return;
            IsSaving = false;
            Enabled = mqtt.Enabled;
            IpAddress = mqtt.IpAddress;
            PortNumber = mqtt.Port;
            Login = mqtt.Login;
            Password = mqtt.Password;
            LastError = mqtt.LastError;
        }

        private void Save()
        {
            IsSaving = true;
            _node.UpdateMqttSettings(Enabled, IpAddress, PortNumber, Login, Password);
        }
    }
}
