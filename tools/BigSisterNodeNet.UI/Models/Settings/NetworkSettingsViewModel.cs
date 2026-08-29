using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Data;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace BigSisterNodeNet.UI.Models.Settings
{
    public partial class NetworkSettingsViewModel : ObservableObject
    {
        private INode _node;
        
        public bool IsUpdated 
        {
            get 
            {
                if(_node.Wifi != null)
                {
                    if (Enabled != _node.Wifi.Enabled) return true;
                    if (WifiSSID != _node.Wifi.WifiSSID) return true;
                    if (Password != _node.Wifi.Password) return true;
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
        private string wifiSSID;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string password;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsUpdated))]
        private string lastError;


        public ICommand GoSave { get; }

        public NetworkSettingsViewModel(INode node)
        {
            _node = node;
            if(_node.Wifi != null)
            {
                Enabled = _node.Wifi.Enabled;
                WifiSSID = _node.Wifi.WifiSSID;
                Password = _node.Wifi.Password;
                LastError = _node.Wifi.LastError;
            }
            
            GoSave = new RelayCommand(Save);
        }

        public void Update(WifiSettings wifi)
        {
            if (wifi == null) return;
            IsSaving = false;
            Enabled = wifi.Enabled;
            WifiSSID = wifi.WifiSSID;
            Password = wifi.Password;
            LastError = wifi.LastError;
        }

        private void Save()
        {
            IsSaving = true;
            _node.UpdateNetworkSettings(Enabled, WifiSSID, Password);
        }

    }
}
