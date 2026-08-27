using BigSisterNodeNet.Core.Models;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core
{
    public partial class NodeNetMessage
    {
        [JsonProperty("from")]
        public byte From { get; set; }

        [JsonProperty("to")]
        public byte To { get; set; }

        [JsonProperty("relayBy")]
        public byte? RelayBy { get; set; }

        [JsonProperty("deviceId")]
        public string DeviceId { get; set; }

        [JsonProperty("rs485Terminator")]
        public bool? Rs485Terminator { get; set; }

        [JsonProperty("instrumentName")]
        public string? InstrumentName { get; set; }

        [JsonProperty("cmd")]
        public string Command { get; set; }

        [JsonProperty("temperature")]
        public float? Temperature { get; set; }

        [JsonProperty("status")]
        public Status? Status { get; set; }

        [JsonProperty("hardwareType")]
        public HardwareType? HardwareType { get; set; }

        [JsonProperty("master")]
        public bool? Master { get; set; }

        [JsonProperty("features")]
        public Dictionary<string, bool>? Features { get; set; }

        // For commands that have a property name and value, like "setProperty"

        [JsonProperty("propertyName")]
        public string? PropertyName { get; set; }

        [JsonProperty("value")]
        public object? Value { get; set; }

        [JsonExtensionData]
        private IDictionary<string, JToken> _extensionData;

        public void SetExtensionValue(string key, object value)
        {
            if (string.IsNullOrWhiteSpace(key))
            {
                throw new ArgumentException("An extension-data key is required.", nameof(key));
            }

            if (_extensionData == null)
            {
                _extensionData = new Dictionary<string, JToken>(StringComparer.Ordinal);
            }

            _extensionData[key] = value == null
                ? JValue.CreateNull()
                : JToken.FromObject(value);
        }

        public bool TryGetExtensionValue(string key, out object value)
        {
            value = null;
            if (_extensionData == null || string.IsNullOrWhiteSpace(key) || !_extensionData.TryGetValue(key, out var token))
            {
                return false;
            }

            value = token.Type == JTokenType.Null
                ? null
                : token.ToObject<object>();
            return true;
        }

        public IDictionary<string, object> ToDictionary()
        {
            var result = new Dictionary<string, object>(StringComparer.Ordinal)
            {
                ["from"] = From,
                ["to"] = To,
            };

            if (RelayBy.HasValue)
            {
                result["relayBy"] = RelayBy.Value;
            }

            if (!string.IsNullOrWhiteSpace(DeviceId))
            {
                result["deviceId"] = DeviceId;
            }

            if (Rs485Terminator.HasValue)
            {
                result["rs485Terminator"] = Rs485Terminator.Value;
            }

            if (!string.IsNullOrWhiteSpace(InstrumentName))
            {
                result["instrumentName"] = InstrumentName;
            }

            if (!string.IsNullOrWhiteSpace(Command))
            {
                result["cmd"] = Command;
            }

            if (Temperature.HasValue)
            {
                result["temperature"] = Temperature.Value;
            }

            if (Status.HasValue)
            {
                result["status"] = Status.Value;
            }

            if (HardwareType.HasValue)
            {
                result["hardwareType"] = HardwareType.Value;
            }

            if (Master.HasValue)
            {
                result["master"] = Master.Value;
            }

            if (Features != null)
            {
                result["features"] = Features;
            }

            if (!string.IsNullOrWhiteSpace(PropertyName))
            {
                result["propertyName"] = PropertyName;
            }

            if (Value != null)
            {
                result["value"] = Value;
            }

            if (_extensionData != null)
            {
                foreach (var entry in _extensionData)
                {
                    result[entry.Key] = entry.Value.Type == JTokenType.Null
                        ? null
                        : entry.Value.ToObject<object>();
                }
            }

            return result;
        }

    }

}
