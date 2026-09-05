using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.Core.PlcCore;
using BigSisterNodeNet.Core.Services;
using BigSisterNodeNet.Plc;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Instruments
{
    public class NodeNet_SOC : Node
    {
        [JsonIgnore]
        private readonly HashSet<string> _loadedPointDefinitionPaths = new HashSet<string>(StringComparer.Ordinal);

        public NodeNet_SOC()
        {
            PointDefinitions = new List<PointDefinition>();
            PointDefinitionTree = new List<PointDefinitionTreeNode>();
            PointStates = new List<PointState>();
        }

        public NodeNet_SOC(NodeNetMessage message) : base(message) 
        {
            PointDefinitions = new List<PointDefinition>();
            PointDefinitionTree = new List<PointDefinitionTreeNode>();
            PointStates = new List<PointState>();
            Update(message);
        }

        [JsonProperty("pointDefinitions")]
        public List<PointDefinition> PointDefinitions { get; set; }

        [JsonIgnore]
        public List<PointDefinitionTreeNode> PointDefinitionTree { get; }

        [JsonProperty("pointStates")]
        public List<PointState> PointStates { get; set; }

        public void BrowsePointDefinitions(string path = "", int limit = 100)
        {
            PointCatalogServiceLocator.Current?.RequestPointDefinitions(this, path, 0, limit);
        }

        public void BrowsePointStates(string path = "", int limit = 100)
        {
            PointCatalogServiceLocator.Current?.RequestPointStates(this, path, 0, limit);
        }

        public void SavePointDefinition(PointDefinition definition)
        {
            PointCatalogServiceLocator.Current?.UpsertPointDefinition(this, definition);
        }

        public void UpdatePointValue(string propertyPath, object value)
        {
            if (string.IsNullOrWhiteSpace(propertyPath))
            {
                return;
            }

            NodeUpdateServiceLocator.Current?.UpdateNode(this, Address, propertyPath, value);
        }

        public PlcUploadResult UploadProgram(string program, ushort slot)
        {
            return UploadProgram(program, slot, null);
        }

        public PlcUploadResult UploadProgram(string program, ushort slot, PlcObjectFileOptions objectFileOptions)
        {
            return PlcProgramUploadServiceLocator.Current?.UploadProgram(this, program, slot, objectFileOptions);
        }

        public PlcDownloadResult DownloadProgramBytecode(ushort slot)
        {
            return PlcProgramUploadServiceLocator.Current?.DownloadProgramBytecode(this, slot);
        }

        public PlcDownloadResult DownloadProgramObjectFile(ushort slot)
        {
            return PlcProgramUploadServiceLocator.Current?.DownloadProgramObjectFile(this, slot);
        }

        public PlcEraseResult EraseProgramSlot(ushort slot)
        {
            return PlcProgramUploadServiceLocator.Current?.EraseProgramSlot(this, slot);
        }

        public bool IsPointDefinitionPathLoaded(string path)
        {
            return _loadedPointDefinitionPaths.Contains(NormalizePath(path));
        }

        public void MarkPointDefinitionPathRequested(string path)
        {
            _loadedPointDefinitionPaths.Remove(NormalizePath(path));
        }

        public void ApplyPointDefinitionsResponse(PointDefinitionsResponse response)
        {
            if (response == null)
            {
                return;
            }

            var normalizedPath = NormalizePath(response.Path);

            if (PointDefinitions == null)
            {
                PointDefinitions = new List<PointDefinition>();
            }

            if (string.Equals(response.Kind, "devices", StringComparison.OrdinalIgnoreCase))
            {
                ApplyDeviceBrowse(response.Devices);
            }

            var pointDefinitions = GetPointDefinitions(response);
            foreach (var definition in pointDefinitions)
            {
                UpsertPointDefinition(definition);
                UpsertPointDefinitionTree(definition);
            }

            if (!response.HasMore)
            {
                _loadedPointDefinitionPaths.Add(normalizedPath);
            }

            OnPropertyChanged(nameof(PointDefinitions));
            OnPropertyChanged(nameof(PointDefinitionTree));
        }

        private static IEnumerable<PointDefinition> GetPointDefinitions(PointDefinitionsResponse response)
        {
            if (response == null)
            {
                return Enumerable.Empty<PointDefinition>();
            }

            if (response.PointFeatures != null && response.PointFeatures.Count > 0)
            {
                return response.PointFeatures
                    .Where(group => group != null)
                    .SelectMany(group => (group.Points ?? Enumerable.Empty<PointDefinition>())
                        .Select(definition => ApplyPointDefinitionScope(definition, response.DeviceId, group.Feature)));
            }

            if (response.Points != null && response.Points.Count > 0)
            {
                return response.Points;
            }

            return response.Definitions ?? Enumerable.Empty<PointDefinition>();
        }

        private void ApplyDeviceBrowse(IEnumerable<PointDefinitionBrowseDevice> devices)
        {
            foreach (var device in devices ?? Enumerable.Empty<PointDefinitionBrowseDevice>())
            {
                if (string.IsNullOrWhiteSpace(device?.DeviceId))
                {
                    continue;
                }

                var deviceNode = GetOrCreateNode(PointDefinitionTree, device.DeviceId, PointDefinitionTreeNodeType.Device, device.DeviceId);
                foreach (var feature in device.Features ?? Enumerable.Empty<string>())
                {
                    var featurePath = BuildFeaturePath(device.DeviceId, feature);
                    GetOrCreateNode(deviceNode.Children, feature, PointDefinitionTreeNodeType.Feature, featurePath);
                }
            }
        }

        public void ApplyPointStatesPage(IEnumerable<PointState> states, bool reset)
        {
            if (PointStates == null)
            {
                PointStates = new List<PointState>();
            }

            if (reset)
            {
                PointStates.Clear();
            }

            foreach (var state in states ?? Enumerable.Empty<PointState>())
            {
                var existing = PointStates.FirstOrDefault(x => x.DeviceId == state.DeviceId && x.Feature == state.Feature && x.PointId == state.PointId);
                if (existing != null)
                {
                    PointStates.Remove(existing);
                }

                PointStates.Add(state);
            }

            OnPropertyChanged(nameof(PointStates));
        }

        public void ApplyPointStatesResponse(PointStatesResponse response)
        {
            if (response == null)
            {
                return;
            }

            ApplyPointStatesPage(GetPointStates(response), response.Offset <= 0);
        }

        private static IEnumerable<PointState> GetPointStates(PointStatesResponse response)
        {
            if (response == null)
            {
                return Enumerable.Empty<PointState>();
            }

            if (response.PointFeatures != null && response.PointFeatures.Count > 0)
            {
                return response.PointFeatures
                    .Where(group => group != null)
                    .SelectMany(group => (group.PointStates ?? Enumerable.Empty<PointState>())
                        .Select(state => ApplyPointStateScope(state, response.DeviceId, group.Feature)));
            }

            if (response.PointStates != null && response.PointStates.Count > 0)
            {
                return response.PointStates;
            }

            return response.States ?? Enumerable.Empty<PointState>();
        }

        private static PointDefinition ApplyPointDefinitionScope(PointDefinition definition, string deviceId, string feature)
        {
            if (definition == null)
            {
                return null;
            }

            if (string.IsNullOrWhiteSpace(definition.DeviceId))
            {
                definition.DeviceId = deviceId;
            }

            if (string.IsNullOrWhiteSpace(definition.Feature))
            {
                definition.Feature = feature;
            }

            return definition;
        }

        private static PointState ApplyPointStateScope(PointState state, string deviceId, string feature)
        {
            if (state == null)
            {
                return null;
            }

            if (string.IsNullOrWhiteSpace(state.DeviceId))
            {
                state.DeviceId = deviceId;
            }

            if (string.IsNullOrWhiteSpace(state.Feature))
            {
                state.Feature = feature;
            }

            return state;
        }

        private void UpsertPointDefinition(PointDefinition definition)
        {
            if (definition == null)
            {
                return;
            }

            var existing = PointDefinitions.FirstOrDefault(x => x.DeviceId == definition.DeviceId && x.Feature == definition.Feature && x.PointId == definition.PointId);
            if (existing != null)
            {
                PointDefinitions.Remove(existing);
            }

            PointDefinitions.Add(definition);
        }

        private void UpsertPointDefinitionTree(PointDefinition definition)
        {
            if (definition == null || string.IsNullOrWhiteSpace(definition.DeviceId))
            {
                return;
            }

            var deviceNode = GetOrCreateNode(PointDefinitionTree, definition.DeviceId, PointDefinitionTreeNodeType.Device, definition.DeviceId);
            var featurePath = BuildFeaturePath(definition.DeviceId, definition.Feature);
            var featureNode = GetOrCreateNode(deviceNode.Children, definition.Feature, PointDefinitionTreeNodeType.Feature, featurePath);
            var pointPath = definition.Path;
            var pointNode = GetOrCreateNode(featureNode.Children, definition.PointId, PointDefinitionTreeNodeType.Point, pointPath);

            pointNode.Definition = definition;
        }

        private static PointDefinitionTreeNode GetOrCreateNode(ICollection<PointDefinitionTreeNode> nodes, string name, PointDefinitionTreeNodeType nodeType, string path)
        {
            var existing = nodes.FirstOrDefault(x => x.NodeType == nodeType && x.Path == path);
            if (existing != null)
            {
                return existing;
            }

            var node = new PointDefinitionTreeNode
            {
                Name = name,
                NodeType = nodeType,
                Path = path
            };

            nodes.Add(node);
            return node;
        }

        private static string BuildFeaturePath(string deviceId, string feature)
        {
            if (string.IsNullOrWhiteSpace(deviceId))
            {
                return string.Empty;
            }

            if (string.IsNullOrWhiteSpace(feature))
            {
                return deviceId;
            }

            return deviceId + "." + feature;
        }

        private static string NormalizePath(string path)
        {
            return path?.Trim() ?? string.Empty;
        }
    }
}
