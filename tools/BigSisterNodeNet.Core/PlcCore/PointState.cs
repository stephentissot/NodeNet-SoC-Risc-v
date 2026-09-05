using Newtonsoft.Json;
using System;

namespace BigSisterNodeNet.Core.PlcCore
{
    public class PointState
    {
        [JsonProperty("deviceId")]
        public string DeviceId { get; set; }

        [JsonProperty("feature")]
        public string Feature { get; set; }

        [JsonProperty("pointId")]
        public string PointId { get; set; }

        [JsonProperty("quality")]
        public PointQuality Quality { get; set; } = PointQuality.Unknown;

        [JsonProperty("upMs")]
        public uint LastUpdateAgeMs { get; set; }

        [JsonProperty("goodMs")]
        public uint LastGoodUpdateAgeMs { get; set; }

        [JsonProperty("value")]
        public object Value { get; set; }

        [JsonIgnore]
        public PointIdentity Id
        {
            get => new PointIdentity
            {
                DeviceId = DeviceId,
                Feature = Feature,
                PointId = PointId
            };
            set
            {
                if (value == null)
                {
                    return;
                }

                DeviceId = value.DeviceId;
                Feature = value.Feature;
                PointId = value.PointId;
            }
        }

        [JsonIgnore]
        public string Path => string.IsNullOrWhiteSpace(DeviceId)
            ? null
            : string.Join(".", new[] { DeviceId, Feature, PointId });
    }
}
