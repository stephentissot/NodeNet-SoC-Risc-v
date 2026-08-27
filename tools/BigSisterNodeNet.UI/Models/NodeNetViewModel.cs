using BigSisterNodeNet.Core;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO.Ports;
using System.Linq;
using System.Runtime.Remoting.Contexts;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.Input;
using BigSisterNodeNet.UI.Models.Instruments;
using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.Models;
using CommunityToolkit.Mvvm.ComponentModel;

namespace BigSisterNodeNet.UI.Models
{
    public partial class NodeNetViewModel : ObservableObject, ITabViewModel
    {
        public bool IsEnabled { get { return true; } }

        public string DeviceId { get { return "WinDriver"; } }

        private ObservableCollection<TabItemViewModel> Tabs { get; set; }
        private TabItemViewModel SelectedTab;

        public ObservableCollection<NodeViewModel> Nodes{ get; } = new ObservableCollection<NodeViewModel>();
        public ObservableCollection<string> Coms { get; } = new ObservableCollection<string>();
        
        private bool _selectAllNodeType;
        public bool SelectAllNodeType 
        { get => _selectAllNodeType;
            set
            {
                if (SetProperty(ref _selectAllNodeType, value))
                {
                    foreach (var node in Nodes)
                    {
                        if (value || node.Node.HardwareType == HardwareType)
                        {
                            node.IsVisible = true;
                        }
                        else
                        {
                            node.IsVisible = false;
                        }
                    }
                    foreach(var tab in Tabs)
                    {
                        if (tab.Content is NodeViewModel nodeVM)
                        {
                            if (value || nodeVM.Node.HardwareType == HardwareType)
                            {
                                tab.IsVisible = true;
                            }
                            else
                            {
                                tab.IsVisible = false;
                            }
                        }
                    }
                }
            }
        }

        public HardwareType HardwareType { get;}

        public RelayCommand ConnectCommand { get; }

        public ICommand DisconnectCommand { get; }

        [ObservableProperty]
        public partial string CoreMessage { get; set; }

        public string SelectedCom { get; set; }

        private string _port;
        private readonly NodeNetCore _nodeNet;

        private EventHandler<NodeViewModel> _nodeViewModelAdded;

        public NodeNetViewModel(NodeNetCore nodeNet, ObservableCollection<TabItemViewModel> tabs, TabItemViewModel selectedTab, HardwareType hardwareType)
        {
            _nodeNet = nodeNet;
            _nodeNet.NodeUpdated += NodeUpdated;
            _nodeNet.NodeHeartbeat += NodeHeartbeat;
            _nodeNet.CoreMessageEvent += OnCoreMessageEvent;
            Tabs = tabs;
            SelectedTab = selectedTab;
            HardwareType = hardwareType;
            if (HardwareType == HardwareType.Undefinded)
                SelectAllNodeType = true;

            foreach (var port in SerialPort.GetPortNames())
            {
                Coms.Add(port);
            }
            if (Coms.Count > 0)
                SelectedCom = Coms[0];
            if (!string.IsNullOrEmpty(_nodeNet.GetPort()))
            {
                _port = _nodeNet.GetPort();
                SelectedCom = _port;
            }
            // Buttons
            ConnectCommand = new RelayCommand(Connect);
        }


        private void NodeHeartbeat(object sender, INode e)
        {
            Application.Current.Dispatcher.Invoke(() =>
            {
                var master = Nodes.FirstOrDefault(n => n.Node.Master);
                if (master != null) master.MasterHeartbeat = true;
                var node = Nodes.FirstOrDefault(n => n.Node.Address == e.Address);
                if(node != null)
                {
                    node.Heartbeat = true;
                    _ = Task.Run(async () =>
                    {
                        await Task.Delay(1000);

                        Application.Current.Dispatcher.Invoke(() =>
                        {                            
                            if (master != null) master.MasterHeartbeat = false;
                            node.Heartbeat = false;
                        });
                    });
                }
            });
            
        }
        private void NodeUpdated(object sender, INode e)
        {
            Application.Current.Dispatcher.Invoke((Delegate)(() =>
            {
                if(Nodes.Any(n => n.Node.Address == e.Address))
                {
                    var nodeVM = Nodes.First(n => n.Node.Address == e.Address);
                    nodeVM.Update(e);                    
                    if (SelectAllNodeType || nodeVM.Node.HardwareType == HardwareType) nodeVM.IsVisible = true;
                    else nodeVM.IsVisible = false;
                    _nodeViewModelAdded?.Invoke(this, nodeVM);
                }
                else
                {
                    switch (e.HardwareType)
                    {
                        case HardwareType.FilterWheel:
                            var filterViewModel = new FilterNodeViewModel(e as FilterNode);
                            if (SelectAllNodeType || filterViewModel.Node.HardwareType == HardwareType) filterViewModel.IsVisible = true;
                            else filterViewModel.IsVisible = false;
                            Nodes.Add(filterViewModel);
                            TabUpdate(filterViewModel);
                            break;
                        case HardwareType.Focuser:
                            var focuserViewModel = new FocuserNodeViewModel(e as FocuserNode);
                            if (SelectAllNodeType || focuserViewModel.Node.HardwareType == HardwareType) focuserViewModel.IsVisible = true;
                            else focuserViewModel.IsVisible = false;
                            Nodes.Add(focuserViewModel);
                            TabUpdate(focuserViewModel);
                            break;
                        case HardwareType.Rotator:
                            var rotatorViewModel = new RotatorNodeViewModel(e as RotatorNode);
                            if (SelectAllNodeType || rotatorViewModel.Node.HardwareType == HardwareType) rotatorViewModel.IsVisible = true;
                            else rotatorViewModel.IsVisible = false;
                            Nodes.Add(rotatorViewModel);
                            TabUpdate(rotatorViewModel);
                            break;
                        case HardwareType.NodeNet_SOC:
                            var nodeNetSocViewModel = new NodeNet_SOCViewModel(e as NodeNet_SOC);
                            if (SelectAllNodeType || nodeNetSocViewModel.Node.HardwareType == HardwareType) nodeNetSocViewModel.IsVisible = true;
                            else nodeNetSocViewModel.IsVisible = false;
                            Nodes.Add(nodeNetSocViewModel);
                            TabUpdate(nodeNetSocViewModel);
                            break;
                        default:
                            var nodeVM = new NodeViewModel(e);
                            if (SelectAllNodeType || nodeVM.Node.HardwareType == HardwareType) nodeVM.IsVisible = true;
                            else nodeVM.IsVisible = false;
                            Nodes.Add(nodeVM);
                            TabUpdate(nodeVM);
                            break;
                    }
                }
            }));
        }

        private void TabUpdate(NodeViewModel nodeVM)
        {
            if (Tabs.Count > 0 && Tabs.Any(x => x.Content.DeviceId == nodeVM.Node.DeviceId))
            {
                var currentTab = Tabs.First(x => x.Content.DeviceId == nodeVM.Node.DeviceId);
                currentTab.Content = nodeVM;
                if (SelectAllNodeType || nodeVM.Node.HardwareType == HardwareType) currentTab.IsVisible = true;
                else currentTab.IsVisible = false;

                currentTab.Header = $"{nodeVM.Node.InstrumentName} ({nodeVM.Node.DeviceId}) #{nodeVM.Node.Address}";
                if (!nodeVM.IsEnabled && SelectedTab.Content.DeviceId == nodeVM.Node.DeviceId)
                {
                    SelectedTab = Tabs[0];
                }
            }
            else
            {
                var tab = new TabItemViewModel(
                    $"{nodeVM.Node.InstrumentName} ({nodeVM.Node.DeviceId}) #{nodeVM.Node.Address}",
                    nodeVM);

                if (SelectAllNodeType || nodeVM.Node.HardwareType == HardwareType) tab.IsVisible = true;
                else tab.IsVisible = false;
                Tabs.Add(tab);
            }
        }
        private void Connect()
        {
            if (string.IsNullOrEmpty(SelectedCom))
                return;
            _nodeNet.SetPort(SelectedCom);
            _nodeNet.Start(SelectedCom);
        }
        private void OnCoreMessageEvent(object sender, string e)
        {
            // Handle core message event
            CoreMessage = e;
        }
    }
}
