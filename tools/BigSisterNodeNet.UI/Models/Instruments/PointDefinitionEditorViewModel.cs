using BigSisterNodeNet.Core.PlcCore;
using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Globalization;
using System.Linq;

namespace BigSisterNodeNet.UI.Models.Instruments
{
    public class PointDefinitionEditorViewModel : ObservableObject
    {
        private PointDefinition _originalDefinition;
        private string _deviceId;
        private string _feature;
        private string _pointId;
        private string _displayName;
        private PointBackend _backend;
        private PointDirection _direction;
        private PointValueType _valueType;
        private string _refreshMs;
        private string _timeoutMs;
        private string _stringCapacity;
        private string _scale;
        private string _unit;
        private string _portIndex;
        private string _slaveAddress;
        private string _address;
        private string _registerCount;
        private ModbusTable _table;
        private ModbusAccess _access;
        private string _remoteDeviceId;
        private string _remoteFeature;
        private string _remotePointId;
        private string _validationMessage;

        public PointDefinitionEditorViewModel(PointDefinition definition)
        {
            Load(definition);
        }

        public Array PointBackends => Enum.GetValues(typeof(PointBackend));
        public Array PointDirections => Enum.GetValues(typeof(PointDirection));
        public Array PointValueTypes => Enum.GetValues(typeof(PointValueType));
        public Array ModbusTables => Enum.GetValues(typeof(ModbusTable));
        public Array ModbusAccesses => Enum.GetValues(typeof(ModbusAccess));

        public string DeviceId { get => _deviceId; set => SetEditableProperty(ref _deviceId, value); }
        public string Feature { get => _feature; set => SetEditableProperty(ref _feature, value); }
        public string PointId { get => _pointId; set => SetEditableProperty(ref _pointId, value); }
        public string DisplayName { get => _displayName; set => SetEditableProperty(ref _displayName, value); }
        public PointBackend Backend { get => _backend; set => SetEditableProperty(ref _backend, value, true); }
        public PointDirection Direction { get => _direction; set => SetEditableProperty(ref _direction, value); }
        public PointValueType ValueType { get => _valueType; set => SetEditableProperty(ref _valueType, value); }
        public string RefreshMs { get => _refreshMs; set => SetEditableProperty(ref _refreshMs, value); }
        public string TimeoutMs { get => _timeoutMs; set => SetEditableProperty(ref _timeoutMs, value); }
        public string StringCapacity { get => _stringCapacity; set => SetEditableProperty(ref _stringCapacity, value); }
        public string Scale { get => _scale; set => SetEditableProperty(ref _scale, value); }
        public string Unit { get => _unit; set => SetEditableProperty(ref _unit, value); }
        public string PortIndex { get => _portIndex; set => SetEditableProperty(ref _portIndex, value); }
        public string SlaveAddress { get => _slaveAddress; set => SetEditableProperty(ref _slaveAddress, value); }
        public string Address { get => _address; set => SetEditableProperty(ref _address, value); }
        public string RegisterCount { get => _registerCount; set => SetEditableProperty(ref _registerCount, value); }
        public ModbusTable Table { get => _table; set => SetEditableProperty(ref _table, value); }
        public ModbusAccess Access { get => _access; set => SetEditableProperty(ref _access, value); }
        public string RemoteDeviceId { get => _remoteDeviceId; set => SetEditableProperty(ref _remoteDeviceId, value); }
        public string RemoteFeature { get => _remoteFeature; set => SetEditableProperty(ref _remoteFeature, value); }
        public string RemotePointId { get => _remotePointId; set => SetEditableProperty(ref _remotePointId, value); }

        public string ValidationMessage
        {
            get => _validationMessage;
            set
            {
                if (SetProperty(ref _validationMessage, value))
                {
                    OnPropertyChanged(nameof(HasValidationMessage));
                }
            }
        }

        public bool HasValidationMessage => !string.IsNullOrWhiteSpace(ValidationMessage);
        public bool HasChanges => !AreEquivalent(ToPointDefinition(), _originalDefinition);
        public bool IsModbusBackend => Backend == PointBackend.Modbus;
        public bool IsNodeNetBackend => Backend == PointBackend.NodeNet;

        public void Load(PointDefinition definition)
        {
            _originalDefinition = Clone(definition) ?? new PointDefinition();
            DeviceId = _originalDefinition.DeviceId;
            Feature = _originalDefinition.Feature;
            PointId = _originalDefinition.PointId;
            DisplayName = _originalDefinition.DisplayName;
            Backend = _originalDefinition.Backend;
            Direction = _originalDefinition.Direction;
            ValueType = _originalDefinition.ValueType;
            RefreshMs = _originalDefinition.RefreshMs.ToString(CultureInfo.InvariantCulture);
            TimeoutMs = _originalDefinition.TimeoutMs.ToString(CultureInfo.InvariantCulture);
            StringCapacity = _originalDefinition.StringCapacity.ToString(CultureInfo.InvariantCulture);
            Scale = _originalDefinition.Scale.ToString(CultureInfo.InvariantCulture);
            Unit = _originalDefinition.Unit;
            PortIndex = ToNullableString(_originalDefinition.PortIndex);
            SlaveAddress = ToNullableString(_originalDefinition.SlaveAddress);
            Address = ToNullableString(_originalDefinition.Address);
            RegisterCount = ToNullableString(_originalDefinition.RegisterCount);
            Table = _originalDefinition.Table ?? ModbusTable.HoldingRegisters;
            Access = _originalDefinition.Access ?? ModbusAccess.Read;
            RemoteDeviceId = _originalDefinition.RemoteDeviceId;
            RemoteFeature = _originalDefinition.RemoteFeature;
            RemotePointId = _originalDefinition.RemotePointId;
            ValidationMessage = string.Empty;
            OnPropertyChanged(nameof(HasChanges));
            OnPropertyChanged(nameof(IsModbusBackend));
            OnPropertyChanged(nameof(IsNodeNetBackend));
        }

        public void AcceptChanges(PointDefinition definition)
        {
            _originalDefinition = Clone(definition) ?? new PointDefinition();
            ValidationMessage = string.Empty;
            OnPropertyChanged(nameof(HasChanges));
        }

        public bool TryBuild(out PointDefinition definition, out string validationMessage)
        {
            definition = null;
            validationMessage = string.Empty;

            var errors = new System.Collections.Generic.List<string>();
            var deviceId = (DeviceId ?? string.Empty).Trim();
            var feature = (Feature ?? string.Empty).Trim();
            var pointId = (PointId ?? string.Empty).Trim();

            if (string.IsNullOrWhiteSpace(deviceId)) errors.Add("DeviceId est obligatoire.");
            if (string.IsNullOrWhiteSpace(feature)) errors.Add("Feature est obligatoire.");
            if (string.IsNullOrWhiteSpace(pointId)) errors.Add("PointId est obligatoire.");
            if (!TryParseUInt(RefreshMs, out var refreshMs)) errors.Add("RefreshMs doit être un entier positif.");
            if (!TryParseUInt(TimeoutMs, out var timeoutMs)) errors.Add("TimeoutMs doit être un entier positif.");
            if (!TryParseUShort(StringCapacity, out var stringCapacity)) errors.Add("StringCapacity doit être un entier positif.");
            if (!TryParseFloat(Scale, out var scale)) errors.Add("Scale doit être un nombre valide.");

            byte? portIndex = null;
            byte? slaveAddress = null;
            ushort? address = null;
            byte? registerCount = null;

            if (Backend == PointBackend.Modbus)
            {
                if (!TryParseNullableByte(PortIndex, out portIndex)) errors.Add("PortIndex doit être un entier valide.");
                if (!TryParseNullableByte(SlaveAddress, out slaveAddress)) errors.Add("SlaveAddress doit être un entier valide.");
                if (!TryParseNullableUShort(Address, out address)) errors.Add("Address doit être un entier valide.");
                if (!TryParseNullableByte(RegisterCount, out registerCount)) errors.Add("RegisterCount doit être un entier valide.");

                if (!portIndex.HasValue) errors.Add("PortIndex est obligatoire pour un backend Modbus.");
                if (!slaveAddress.HasValue) errors.Add("SlaveAddress est obligatoire pour un backend Modbus.");
                if (!address.HasValue) errors.Add("Address est obligatoire pour un backend Modbus.");
                if (!registerCount.HasValue) errors.Add("RegisterCount est obligatoire pour un backend Modbus.");
            }

            var remoteDeviceId = (RemoteDeviceId ?? string.Empty).Trim();
            var remoteFeature = (RemoteFeature ?? string.Empty).Trim();
            var remotePointId = (RemotePointId ?? string.Empty).Trim();

            if (Backend == PointBackend.NodeNet)
            {
                if (string.IsNullOrWhiteSpace(remoteDeviceId)) errors.Add("RemoteDeviceId est obligatoire pour un backend NodeNet.");
                if (string.IsNullOrWhiteSpace(remoteFeature)) errors.Add("RemoteFeature est obligatoire pour un backend NodeNet.");
                if (string.IsNullOrWhiteSpace(remotePointId)) errors.Add("RemotePointId est obligatoire pour un backend NodeNet.");
            }

            if (errors.Any())
            {
                validationMessage = string.Join(" ", errors);
                return false;
            }

            definition = new PointDefinition
            {
                DeviceId = deviceId,
                Feature = feature,
                PointId = pointId,
                DisplayName = NormalizeString(DisplayName),
                Backend = Backend,
                Direction = Direction,
                ValueType = ValueType,
                RefreshMs = refreshMs,
                TimeoutMs = timeoutMs,
                StringCapacity = stringCapacity,
                Scale = scale,
                Unit = NormalizeString(Unit),
                PortIndex = Backend == PointBackend.Modbus ? portIndex : null,
                SlaveAddress = Backend == PointBackend.Modbus ? slaveAddress : null,
                Address = Backend == PointBackend.Modbus ? address : null,
                RegisterCount = Backend == PointBackend.Modbus ? registerCount : null,
                Table = Backend == PointBackend.Modbus ? Table : null,
                Access = Backend == PointBackend.Modbus ? Access : null,
                RemoteDeviceId = Backend == PointBackend.NodeNet ? remoteDeviceId : null,
                RemoteFeature = Backend == PointBackend.NodeNet ? remoteFeature : null,
                RemotePointId = Backend == PointBackend.NodeNet ? remotePointId : null
            };

            return true;
        }

        private bool SetEditableProperty<T>(ref T field, T value, bool refreshBackendFlags = false, string propertyName = null)
        {
            if (!SetProperty(ref field, value, propertyName))
            {
                return false;
            }

            ValidationMessage = string.Empty;
            OnPropertyChanged(nameof(HasChanges));
            if (refreshBackendFlags)
            {
                OnPropertyChanged(nameof(IsModbusBackend));
                OnPropertyChanged(nameof(IsNodeNetBackend));
            }

            return true;
        }

        private PointDefinition ToPointDefinition()
        {
            TryBuild(out var definition, out _);
            return definition;
        }

        private static PointDefinition Clone(PointDefinition definition)
        {
            if (definition == null)
            {
                return null;
            }

            return new PointDefinition
            {
                DeviceId = definition.DeviceId,
                Feature = definition.Feature,
                PointId = definition.PointId,
                DisplayName = definition.DisplayName,
                Backend = definition.Backend,
                Direction = definition.Direction,
                ValueType = definition.ValueType,
                RefreshMs = definition.RefreshMs,
                TimeoutMs = definition.TimeoutMs,
                StringCapacity = definition.StringCapacity,
                Scale = definition.Scale,
                Unit = definition.Unit,
                PortIndex = definition.PortIndex,
                SlaveAddress = definition.SlaveAddress,
                Address = definition.Address,
                RegisterCount = definition.RegisterCount,
                Table = definition.Table,
                Access = definition.Access,
                RemoteDeviceId = definition.RemoteDeviceId,
                RemoteFeature = definition.RemoteFeature,
                RemotePointId = definition.RemotePointId
            };
        }

        private static bool AreEquivalent(PointDefinition left, PointDefinition right)
        {
            if (ReferenceEquals(left, right))
            {
                return true;
            }

            if (left == null || right == null)
            {
                return false;
            }

            return string.Equals(NormalizeString(left.DeviceId), NormalizeString(right.DeviceId), StringComparison.Ordinal)
                && string.Equals(NormalizeString(left.Feature), NormalizeString(right.Feature), StringComparison.Ordinal)
                && string.Equals(NormalizeString(left.PointId), NormalizeString(right.PointId), StringComparison.Ordinal)
                && string.Equals(NormalizeString(left.DisplayName), NormalizeString(right.DisplayName), StringComparison.Ordinal)
                && left.Backend == right.Backend
                && left.Direction == right.Direction
                && left.ValueType == right.ValueType
                && left.RefreshMs == right.RefreshMs
                && left.TimeoutMs == right.TimeoutMs
                && left.StringCapacity == right.StringCapacity
                && Math.Abs(left.Scale - right.Scale) < 0.0001f
                && string.Equals(NormalizeString(left.Unit), NormalizeString(right.Unit), StringComparison.Ordinal)
                && left.PortIndex == right.PortIndex
                && left.SlaveAddress == right.SlaveAddress
                && left.Address == right.Address
                && left.RegisterCount == right.RegisterCount
                && left.Table == right.Table
                && left.Access == right.Access
                && string.Equals(NormalizeString(left.RemoteDeviceId), NormalizeString(right.RemoteDeviceId), StringComparison.Ordinal)
                && string.Equals(NormalizeString(left.RemoteFeature), NormalizeString(right.RemoteFeature), StringComparison.Ordinal)
                && string.Equals(NormalizeString(left.RemotePointId), NormalizeString(right.RemotePointId), StringComparison.Ordinal);
        }

        private static string ToNullableString<T>(T? value) where T : struct
        {
            return value.HasValue ? Convert.ToString(value.Value, CultureInfo.InvariantCulture) : string.Empty;
        }

        private static string NormalizeString(string value)
        {
            var trimmed = value?.Trim();
            return string.IsNullOrWhiteSpace(trimmed) ? null : trimmed;
        }

        private static bool TryParseUInt(string value, out uint result)
        {
            return uint.TryParse((value ?? string.Empty).Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out result);
        }

        private static bool TryParseUShort(string value, out ushort result)
        {
            return ushort.TryParse((value ?? string.Empty).Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out result);
        }

        private static bool TryParseNullableByte(string value, out byte? result)
        {
            result = null;
            var trimmed = (value ?? string.Empty).Trim();
            if (string.IsNullOrWhiteSpace(trimmed))
            {
                return true;
            }

            if (byte.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                result = parsed;
                return true;
            }

            return false;
        }

        private static bool TryParseNullableUShort(string value, out ushort? result)
        {
            result = null;
            var trimmed = (value ?? string.Empty).Trim();
            if (string.IsNullOrWhiteSpace(trimmed))
            {
                return true;
            }

            if (ushort.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed))
            {
                result = parsed;
                return true;
            }

            return false;
        }

        private static bool TryParseFloat(string value, out float result)
        {
            var trimmed = (value ?? string.Empty).Trim();
            return float.TryParse(trimmed, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.CurrentCulture, out result)
                || float.TryParse(trimmed, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture, out result);
        }
    }
}
