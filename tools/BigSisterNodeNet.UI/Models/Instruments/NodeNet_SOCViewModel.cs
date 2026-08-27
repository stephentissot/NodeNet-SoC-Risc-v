using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.Core.PlcCore;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Input;

namespace BigSisterNodeNet.UI.Models.Instruments
{
    public partial class NodeNet_SOCViewModel : NodeViewModel
    {
        private NodeNet_SOC _nodeNetSoc;

        [ObservableProperty]
        public partial string BrowsePath { get; set; }

        public ICommand BrowsePathCommand { get; }
        public ICommand BrowseRootCommand { get; }
        public ICommand BrowseTreeNodeCommand { get; }
        public ICommand ShowTreeNodeStateCommand { get; }
        public ICommand AddFeatureCommand { get; }
        public ICommand AddPointCommand { get; }
        public ICommand DeleteFeatureCommand { get; }
        public ICommand ShowDefinitionCommand { get; }
        public ICommand DeletePointCommand { get; }
        public ICommand ReadStatesCommand { get; }
        public IRelayCommand UpdatePointDefinitionCommand { get; }
        public IRelayCommand UploadPlcProgramCommand { get; }

        private int _selectedPlcSlot;
        private string _plcProgramSource;
        private string _plcStatusMessage;
        private bool _isUploadingPlcProgram;

        private PointDefinitionEditorViewModel _settingsEditor;
        public PointDefinitionEditorViewModel SettingsEditor
        {
            get => _settingsEditor;
            private set
            {
                if (_settingsEditor == value)
                {
                    return;
                }

                if (_settingsEditor != null)
                {
                    _settingsEditor.PropertyChanged -= SettingsEditor_PropertyChanged;
                }

                _settingsEditor = value;

                if (_settingsEditor != null)
                {
                    _settingsEditor.PropertyChanged += SettingsEditor_PropertyChanged;
                }

                OnPropertyChanged(nameof(SettingsEditor));
                UpdatePointDefinitionCommand.NotifyCanExecuteChanged();
            }
        }

        private bool _isSettingsVisible;
        public bool IsSettingsVisible
        {
            get => _isSettingsVisible;
            private set
            {
                if (SetProperty(ref _isSettingsVisible, value))
                {
                    OnPropertyChanged(nameof(IsPointStatesVisible));
                    OnPropertyChanged(nameof(DetailsTitle));
                }
            }
        }

        public bool IsPointStatesVisible => !IsSettingsVisible;
        public string DetailsTitle => IsSettingsVisible ? "Settings" : "PointStates";

        public ObservableCollection<PointDefinitionTreeNode> PointDefinitionTree { get; } = new ObservableCollection<PointDefinitionTreeNode>();

        public ObservableCollection<PointState> PointStates { get; } = new ObservableCollection<PointState>();
        public ObservableCollection<PointStateViewModel> PointStateRows { get; } = new ObservableCollection<PointStateViewModel>();
        public ObservableCollection<int> PlcSlotChoices { get; } = new ObservableCollection<int>(Enumerable.Range(0, 16));

        public int SelectedPlcSlot
        {
            get => _selectedPlcSlot;
            set => SetProperty(ref _selectedPlcSlot, value);
        }

        public string PlcProgramSource
        {
            get => _plcProgramSource;
            set => SetProperty(ref _plcProgramSource, value);
        }

        public string PlcStatusMessage
        {
            get => _plcStatusMessage;
            private set => SetProperty(ref _plcStatusMessage, value);
        }

        public string PlcHintMessage => "Upload direct vers le slot via objectFileV1. `PARAM POINT_ID` nécessite encore des bindings fournis par code.";

        public bool IsUploadingPlcProgram
        {
            get => _isUploadingPlcProgram;
            private set
            {
                if (SetProperty(ref _isUploadingPlcProgram, value))
                {
                    UploadPlcProgramCommand.NotifyCanExecuteChanged();
                }
            }
        }

        public NodeNet_SOCViewModel(NodeNet_SOC node) : base(node)
        {
            _nodeNetSoc = node;
            BrowsePath = node?.DeviceId ?? string.Empty;
            SelectedPlcSlot = 0;
            PlcProgramSource = "CONST POINT_ID input, gb9fao5yk4f.modbus0.waveshare8ch.input1\r\nCONST POINT_ID output, gb9fao5yk4f.modbus0.waveshare8ch.output1\r\n\r\nLOAD_BOOL input\r\nSTORE_BOOL output\r\nHALT\r\n";
            PlcStatusMessage = "Prêt à uploader un programme PLC vers le slot sélectionné.";
            BrowsePathCommand = new RelayCommand(BrowsePathDefinitions);
            BrowseRootCommand = new RelayCommand(BrowseRoot);
            BrowseTreeNodeCommand = new RelayCommand<string>(BrowseTreeNode);
            ShowTreeNodeStateCommand = new RelayCommand<string>(ShowTreeNodeState);
            AddFeatureCommand = new RelayCommand<PointDefinitionTreeNode>(AddFeature);
            AddPointCommand = new RelayCommand<PointDefinitionTreeNode>(AddPoint);
            DeleteFeatureCommand = new RelayCommand<PointDefinitionTreeNode>(DeleteFeature);
            ShowDefinitionCommand = new RelayCommand<PointDefinitionTreeNode>(ShowDefinition);
            DeletePointCommand = new RelayCommand<PointDefinitionTreeNode>(DeletePoint);
            ReadStatesCommand = new RelayCommand(ReadStates);
            UpdatePointDefinitionCommand = new RelayCommand(UpdatePointDefinition, CanUpdatePointDefinition);
            UploadPlcProgramCommand = new RelayCommand(UploadPlcProgram, CanUploadPlcProgram);
            RefreshCollections();
        }

        public override void Update(INode node)
        {
            base.Update(node);
            _nodeNetSoc = node as NodeNet_SOC;
            RefreshCollections();
        }

        private void BrowsePathDefinitions()
        {
            _nodeNetSoc?.BrowsePointDefinitions(BrowsePath ?? string.Empty);
        }

        private void BrowseRoot()
        {
            BrowsePath = string.Empty;
            _nodeNetSoc?.BrowsePointDefinitions(string.Empty);
        }

        private void BrowseTreeNode(string path)
        {
            BrowsePath = path ?? string.Empty;
            _nodeNetSoc?.BrowsePointDefinitions(BrowsePath);
        }

        private void ShowTreeNodeState(string path)
        {
            BrowsePath = path ?? string.Empty;
            IsSettingsVisible = false;
            ReadStates();
        }

        private void ReadStates()
        {
            IsSettingsVisible = false;
            _nodeNetSoc?.BrowsePointStates(BrowsePath ?? string.Empty);
        }

        private void AddFeature(PointDefinitionTreeNode node)
        {
        }

        private void AddPoint(PointDefinitionTreeNode node)
        {
        }

        private void DeleteFeature(PointDefinitionTreeNode node)
        {
        }

        private void ShowDefinition(PointDefinitionTreeNode node)
        {
            if (node?.Definition == null)
            {
                return;
            }

            BrowsePath = node.Path ?? string.Empty;
            SettingsEditor = new PointDefinitionEditorViewModel(node.Definition);
            IsSettingsVisible = true;
        }

        private void DeletePoint(PointDefinitionTreeNode node)
        {
        }

        private bool CanUploadPlcProgram()
        {
            return !IsUploadingPlcProgram && !string.IsNullOrWhiteSpace(PlcProgramSource);
        }

        private async void UploadPlcProgram()
        {
            if (_nodeNetSoc == null)
            {
                PlcStatusMessage = "Aucun nœud NodeNet SoC sélectionné.";
                return;
            }

            try
            {
                IsUploadingPlcProgram = true;
                PlcStatusMessage = $"Upload en cours vers le slot {SelectedPlcSlot}...";

                var result = await Task.Run(() => _nodeNetSoc.UploadProgram(PlcProgramSource ?? string.Empty, (ushort)SelectedPlcSlot));
                var loadStatus = ReadResponseValue(result?.CommitResponse, "loadStatus");
                PlcStatusMessage = string.IsNullOrWhiteSpace(loadStatus)
                    ? $"Upload terminé sur le slot {SelectedPlcSlot}."
                    : $"Upload terminé sur le slot {SelectedPlcSlot}. loadStatus={loadStatus}.";
            }
            catch (Exception ex)
            {
                PlcStatusMessage = ex.Message;
            }
            finally
            {
                IsUploadingPlcProgram = false;
            }
        }

        private bool CanUpdatePointDefinition()
        {
            return SettingsEditor?.HasChanges == true;
        }

        private void UpdatePointDefinition()
        {
            if (SettingsEditor == null)
            {
                return;
            }

            if (!SettingsEditor.TryBuild(out var definition, out var validationMessage))
            {
                SettingsEditor.ValidationMessage = validationMessage;
                return;
            }

            _nodeNetSoc?.SavePointDefinition(definition);
            SettingsEditor.AcceptChanges(definition);
            BrowsePath = definition.Path ?? string.Empty;
        }

        private void SettingsEditor_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(PointDefinitionEditorViewModel.HasChanges))
            {
                UpdatePointDefinitionCommand.NotifyCanExecuteChanged();
            }
        }

        public void CommitPointStateValue(PointStateViewModel pointState, object value)
        {
            if (pointState == null || string.IsNullOrWhiteSpace(pointState.PropertyPath))
            {
                return;
            }

            _nodeNetSoc?.UpdatePointValue(pointState.PropertyPath, value);
        }

        private void RefreshCollections()
        {
            PointDefinitionTree.Clear();
            foreach (var treeNode in _nodeNetSoc?.PointDefinitionTree ?? Enumerable.Empty<PointDefinitionTreeNode>())
            {
                PointDefinitionTree.Add(treeNode);
            }

            PointStates.Clear();
            foreach (var pointState in _nodeNetSoc?.PointStates ?? Enumerable.Empty<PointState>())
            {
                PointStates.Add(pointState);
            }

            PointStateRows.Clear();
            foreach (var pointState in _nodeNetSoc?.PointStates ?? Enumerable.Empty<PointState>())
            {
                var definition = _nodeNetSoc?.PointDefinitions?.FirstOrDefault(x => x.DeviceId == pointState.DeviceId && x.Feature == pointState.Feature && x.PointId == pointState.PointId);
                PointStateRows.Add(new PointStateViewModel(pointState, definition, CommitPointStateValue));
            }
        }

        private static string ReadResponseValue(System.Collections.Generic.IDictionary<string, object> response, string key)
        {
            if (response == null || !response.TryGetValue(key, out var value) || value == null)
            {
                return string.Empty;
            }

            return Convert.ToString(value);
        }
    }
}
