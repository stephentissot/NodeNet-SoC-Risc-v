using BigSisterNodeNet.Core.PlcCore;
using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Globalization;

namespace BigSisterNodeNet.UI.Models.Instruments
{
    public partial class PointStateViewModel : ObservableObject
    {
        private readonly Action<PointStateViewModel, object> _commitValueAction;

        public PointStateViewModel(PointState state, PointDefinition definition, Action<PointStateViewModel, object> commitValueAction)
        {
            State = state;
            Definition = definition;
            _commitValueAction = commitValueAction;
            RefreshFromModel();
        }

        public PointState State { get; }
        public PointDefinition Definition { get; }

        public string DeviceId => State?.DeviceId;
        public string Feature => State?.Feature;
        public string PointId => State?.PointId;
        public PointQuality Quality => State?.Quality ?? PointQuality.Unknown;
        public uint LastUpdateAgeMs => State?.LastUpdateAgeMs ?? 0;
        public uint LastGoodUpdateAgeMs => State?.LastGoodUpdateAgeMs ?? 0;
        public string PropertyPath => State?.Path ?? Definition?.Path;

        [ObservableProperty]
        private string editableTextValue;

        [ObservableProperty]
        private bool? editableBoolValue;

        public bool CanEdit => Definition != null && (Definition.Direction == PointDirection.Output || Definition.Direction == PointDirection.InOut);
        public bool IsEditableBool => CanEdit && Definition?.ValueType == PointValueType.Bool;
        public bool IsEditableNumeric => CanEdit && Definition != null && IsNumericValueType(Definition.ValueType);
        public bool IsReadOnlyValue => !IsEditableBool && !IsEditableNumeric;
        public string DisplayValue => State?.Value?.ToString() ?? string.Empty;

        public void RefreshFromModel()
        {
            EditableTextValue = DisplayValue;
            EditableBoolValue = ToNullableBoolean(State?.Value);
            OnPropertyChanged(nameof(DisplayValue));
            OnPropertyChanged(nameof(CanEdit));
            OnPropertyChanged(nameof(IsEditableBool));
            OnPropertyChanged(nameof(IsEditableNumeric));
            OnPropertyChanged(nameof(IsReadOnlyValue));
        }

        public bool CommitTextEdit()
        {
            if (!IsEditableNumeric || Definition == null)
            {
                return false;
            }

            if (!TryParseNumericValue(EditableTextValue, Definition.ValueType, out var parsedValue))
            {
                RefreshFromModel();
                return false;
            }

            if (AreValuesEqual(State?.Value, parsedValue))
            {
                EditableTextValue = Convert.ToString(parsedValue, CultureInfo.InvariantCulture);
                return false;
            }

            ApplyLocalValue(parsedValue);
            _commitValueAction?.Invoke(this, parsedValue);
            return true;
        }

        public bool CommitBoolEdit(bool? editedValue = null)
        {
            var currentValue = editedValue ?? EditableBoolValue;

            if (!IsEditableBool || !currentValue.HasValue)
            {
                return false;
            }

            EditableBoolValue = currentValue;

            var parsedValue = currentValue.Value;
            if (AreValuesEqual(State?.Value, parsedValue))
            {
                return false;
            }

            ApplyLocalValue(parsedValue);
            _commitValueAction?.Invoke(this, parsedValue);
            return true;
        }

        private void ApplyLocalValue(object value)
        {
            if (State != null)
            {
                State.Value = value;
            }

            EditableTextValue = Convert.ToString(value, CultureInfo.InvariantCulture);
            EditableBoolValue = ToNullableBoolean(value);
            OnPropertyChanged(nameof(DisplayValue));
        }

        private static bool IsNumericValueType(PointValueType valueType)
        {
            return valueType == PointValueType.Uint16
                || valueType == PointValueType.Int16
                || valueType == PointValueType.Uint32
                || valueType == PointValueType.Int32
                || valueType == PointValueType.Float;
        }

        private static bool TryParseNumericValue(string input, PointValueType valueType, out object value)
        {
            value = null;
            var trimmed = (input ?? string.Empty).Trim();

            switch (valueType)
            {
                case PointValueType.Uint16:
                    if (ushort.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var uint16Value))
                    {
                        value = uint16Value;
                        return true;
                    }
                    break;
                case PointValueType.Int16:
                    if (short.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var int16Value))
                    {
                        value = int16Value;
                        return true;
                    }
                    break;
                case PointValueType.Uint32:
                    if (uint.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var uint32Value))
                    {
                        value = uint32Value;
                        return true;
                    }
                    break;
                case PointValueType.Int32:
                    if (int.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var int32Value))
                    {
                        value = int32Value;
                        return true;
                    }
                    break;
                case PointValueType.Float:
                    if (float.TryParse(trimmed, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.CurrentCulture, out var floatValue)
                        || float.TryParse(trimmed, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture, out floatValue))
                    {
                        value = floatValue;
                        return true;
                    }
                    break;
            }

            return false;
        }

        private static bool? ToNullableBoolean(object value)
        {
            if (value is bool booleanValue)
            {
                return booleanValue;
            }

            if (value == null)
            {
                return null;
            }

            if (bool.TryParse(value.ToString(), out var parsedValue))
            {
                return parsedValue;
            }

            return null;
        }

        private static bool AreValuesEqual(object left, object right)
        {
            var leftText = Convert.ToString(left, CultureInfo.InvariantCulture);
            var rightText = Convert.ToString(right, CultureInfo.InvariantCulture);
            return string.Equals(leftText, rightText, StringComparison.OrdinalIgnoreCase);
        }
    }
}
