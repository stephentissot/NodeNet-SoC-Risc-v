using Newtonsoft.Json;
using System;

namespace BigSisterNodeNet.Core.PlcCore
{
    public class PointIdentity
    {
        [JsonProperty("deviceId")]
        public string DeviceId { get; set; }

        [JsonProperty("feature")]
        public string Feature { get; set; }

        [JsonProperty("pointId")]
        public string PointId { get; set; }
    }

    public class PollingSettings
    {
        [JsonProperty("refreshMs")]
        public uint RefreshMs { get; set; } = 1000;

        [JsonProperty("timeoutMs")]
        public uint TimeoutMs { get; set; } = 3000;
    }

    public class ModbusPointRef
    {
        [JsonProperty("portIndex")]
        public byte PortIndex { get; set; }

        [JsonProperty("slaveAddress")]
        public byte SlaveAddress { get; set; } = 1;

        [JsonProperty("address")]
        public ushort Address { get; set; }

        [JsonProperty("registerCount")]
        public byte RegisterCount { get; set; } = 1;

        [JsonProperty("table")]
        public ModbusTable Table { get; set; } = ModbusTable.HoldingRegisters;

        [JsonProperty("access")]
        public ModbusAccess Access { get; set; } = ModbusAccess.Read;
    }

    public class NodeNetPointRef
    {
        [JsonProperty("remoteDeviceId")]
        public string RemoteDeviceId { get; set; }

        [JsonProperty("remoteFeature")]
        public string RemoteFeature { get; set; }

        [JsonProperty("remotePointId")]
        public string RemotePointId { get; set; }
    }

    public class PointDefinition
    {
        [JsonProperty("deviceId")]
        public string DeviceId { get; set; }

        [JsonProperty("feature")]
        public string Feature { get; set; }

        [JsonProperty("pointId")]
        public string PointId { get; set; }

        [JsonProperty("displayName")]
        public string DisplayName { get; set; }

        [JsonProperty("backend")]
        public PointBackend Backend { get; set; }

        [JsonProperty("direction")]
        public PointDirection Direction { get; set; }

        [JsonProperty("valueType")]
        public PointValueType ValueType { get; set; }

        [JsonProperty("refreshMs")]
        public uint RefreshMs { get; set; } = 1000;

        [JsonProperty("timeoutMs")]
        public uint TimeoutMs { get; set; } = 3000;

        [JsonProperty("stringCapacity")]
        public ushort StringCapacity { get; set; }

        [JsonProperty("scale")]
        public float Scale { get; set; } = 1.0f;

        [JsonProperty("unit")]
        public string Unit { get; set; }

        [JsonProperty("portIndex")]
        public byte? PortIndex { get; set; }

        [JsonProperty("slaveAddress")]
        public byte? SlaveAddress { get; set; }

        [JsonProperty("address")]
        public ushort? Address { get; set; }

        [JsonProperty("registerCount")]
        public byte? RegisterCount { get; set; }

        [JsonProperty("table")]
        public ModbusTable? Table { get; set; }

        [JsonProperty("access")]
        public ModbusAccess? Access { get; set; }

        [JsonProperty("remoteDeviceId")]
        public string RemoteDeviceId { get; set; }

        [JsonProperty("remoteFeature")]
        public string RemoteFeature { get; set; }

        [JsonProperty("remotePointId")]
        public string RemotePointId { get; set; }

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
        public PollingSettings Polling
        {
            get => new PollingSettings
            {
                RefreshMs = RefreshMs,
                TimeoutMs = TimeoutMs
            };
            set
            {
                if (value == null)
                {
                    return;
                }

                RefreshMs = value.RefreshMs;
                TimeoutMs = value.TimeoutMs;
            }
        }

        [JsonIgnore]
        public ModbusPointRef ModbusRef
        {
            get => new ModbusPointRef
            {
                PortIndex = PortIndex ?? 0,
                SlaveAddress = SlaveAddress ?? 1,
                Address = Address ?? 0,
                RegisterCount = RegisterCount ?? 1,
                Table = Table ?? ModbusTable.HoldingRegisters,
                Access = Access ?? ModbusAccess.Read
            };
            set
            {
                if (value == null)
                {
                    return;
                }

                PortIndex = value.PortIndex;
                SlaveAddress = value.SlaveAddress;
                Address = value.Address;
                RegisterCount = value.RegisterCount;
                Table = value.Table;
                Access = value.Access;
            }
        }

        [JsonIgnore]
        public NodeNetPointRef NodeNetRef
        {
            get => new NodeNetPointRef
            {
                RemoteDeviceId = RemoteDeviceId,
                RemoteFeature = RemoteFeature,
                RemotePointId = RemotePointId
            };
            set
            {
                if (value == null)
                {
                    return;
                }

                RemoteDeviceId = value.RemoteDeviceId;
                RemoteFeature = value.RemoteFeature;
                RemotePointId = value.RemotePointId;
            }
        }

        [JsonIgnore]
        public string Path => string.IsNullOrWhiteSpace(DeviceId)
            ? null
            : string.Join(".", new[] { DeviceId, Feature, PointId });
    }
}
