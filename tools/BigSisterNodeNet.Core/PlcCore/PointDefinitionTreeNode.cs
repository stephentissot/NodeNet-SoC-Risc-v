using Newtonsoft.Json;
using System.Collections.Generic;
using System.Linq;

namespace BigSisterNodeNet.Core.PlcCore
{
    public enum PointDefinitionTreeNodeType : byte
    {
        Device = 0,
        Feature = 1,
        Point = 2
    }

    public class PointDefinitionTreeNode
    {
        public PointDefinitionTreeNode()
        {
            Children = new List<PointDefinitionTreeNode>();
        }

        [JsonProperty("name")]
        public string Name { get; set; }

        [JsonProperty("path")]
        public string Path { get; set; }

        [JsonProperty("nodeType")]
        public PointDefinitionTreeNodeType NodeType { get; set; }

        [JsonProperty("children")]
        public List<PointDefinitionTreeNode> Children { get; }

        [JsonProperty("definition")]
        public PointDefinition Definition { get; set; }

        [JsonIgnore]
        public bool CanBrowseChildren => NodeType != PointDefinitionTreeNodeType.Point;

        [JsonIgnore]
        public bool HasChildren => Children.Any();

        [JsonIgnore]
        public bool IsExpanded { get; set; }
    }
}
