using Newtonsoft.Json;
using System.Collections.Generic;

namespace BigSisterNodeNet.Core.PlcCore
{
    public class PointDefinitionBrowseDevice
    {
        [JsonProperty("deviceId")]
        public string DeviceId { get; set; }

        [JsonProperty("features")]
        public List<string> Features { get; set; } = new List<string>();
    }

    public abstract class PagedNodeNetResponse<TItem> : NodeNetMessage
    {
        [JsonProperty("path")]
        public string Path { get; set; }

        [JsonProperty("offset")]
        public int Offset { get; set; }

        [JsonProperty("limit")]
        public int Limit { get; set; }

        [JsonProperty("count")]
        public int Count { get; set; }

        [JsonProperty("total")]
        public int Total { get; set; }

        [JsonProperty("hasMore")]
        public bool HasMore { get; set; }
    }

    public class PointDefinitionsResponse : PagedNodeNetResponse<PointDefinition>
    {
        public PointDefinitionsResponse()
        {
            Command = "pointDefsRes";
        }

        [JsonProperty("kind")]
        public string Kind { get; set; }

        [JsonProperty("devices")]
        public List<PointDefinitionBrowseDevice> Devices { get; set; } = new List<PointDefinitionBrowseDevice>();

        [JsonProperty("definitions")]
        public List<PointDefinition> Definitions { get; set; } = new List<PointDefinition>();

        [JsonProperty("points")]
        public List<PointDefinition> Points { get; set; } = new List<PointDefinition>();
    }

    public class PointStatesResponse : PagedNodeNetResponse<PointState>
    {
        public PointStatesResponse()
        {
            Command = "pointStatesRes";
        }

        [JsonProperty("kind")]
        public string Kind { get; set; }

        [JsonProperty("states")]
        public List<PointState> States { get; set; } = new List<PointState>();

        [JsonProperty("pointStates")]
        public List<PointState> PointStates { get; set; } = new List<PointState>();
    }
}
