using Newtonsoft.Json;

namespace BigSisterNodeNet.Core.PlcCore
{
    public abstract class PagedNodeNetRequest : NodeNetMessage
    {
        [JsonProperty("path")]
        public string Path { get; set; }

        [JsonProperty("offset")]
        public int Offset { get; set; }

        [JsonProperty("limit")]
        public int Limit { get; set; }
    }

    public class PointDefinitionsRequest : PagedNodeNetRequest
    {
        public PointDefinitionsRequest()
        {
            Command = NodeNetCommands.PointDefinitionsReq;
        }
    }

    public class PointStatesRequest : PagedNodeNetRequest
    {
        public PointStatesRequest()
        {
            Command = NodeNetCommands.PointStatesReq;
        }
    }
}
