using Newtonsoft.Json;

namespace BigSisterNodeNet.Core.PlcCore
{
    public class PointUpsertRequest : NodeNetMessage
    {
        public PointUpsertRequest()
        {
            Command = NodeNetCommands.PointUpsert;
        }

        [JsonProperty("definition")]
        public PointDefinition Definition { get; set; }
    }
}
