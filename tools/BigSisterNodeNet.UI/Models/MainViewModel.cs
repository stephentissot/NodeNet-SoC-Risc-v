using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.UI.Models.Instruments;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Threading;

namespace BigSisterNodeNet.UI.Models
{
    public partial class MainViewModel : ObservableObject
    {
        public ObservableCollection<TabItemViewModel> Tabs { get; } = new();

        private TabItemViewModel? _selectedTab;

        public TabItemViewModel? SelectedTab
        {
            get => _selectedTab;
            set
            {
                if (SetProperty(ref _selectedTab, value))
                {
                    OnSelectedTabChanged(value);
                }
            }
        }

        private readonly NodeNetCore _nodeNet;
        
        private readonly HardwareType _hardwareType = HardwareType.Undefinded;
        
        public RelayCommand OkCommand { get; }

        public MainViewModel(NodeNetCore nodeNet, HardwareType type)
        {
            _hardwareType = type;
            _nodeNet = nodeNet;

            var nodeNetVM = new NodeNetViewModel(_nodeNet, Tabs, SelectedTab, _hardwareType);

            var nodeNetTab = new TabItemViewModel("NodeNet", nodeNetVM);
            nodeNetTab.IsVisible = true;

            Tabs.Add(nodeNetTab);
            SelectedTab = nodeNetTab;
            OkCommand = new RelayCommand(Ok);
        }

        private void OnSelectedTabChanged(TabItemViewModel? tab)
        {
            if (tab?.Content is NodeViewModel node)
            {
                node.OnSelected();
            }
        }

        private void Ok()
        {
            // Close MainWindow
            Application.Current.MainWindow.Close();
        }
    }
}
