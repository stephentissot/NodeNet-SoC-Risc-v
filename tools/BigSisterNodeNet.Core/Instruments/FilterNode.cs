using BigSisterNodeNet.Core.Extensions;
using BigSisterNodeNet.Core.Models;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Instruments
{
    public class FilterNode : Node
    {

        protected void SendFilterPropertyUpdate<T>(byte Address, System.Linq.Expressions.Expression<Func<FilterNode, T>> propertyExpression, T value)
        {
            // Utilise le service injecté ou le service locator (pour les objets désérialisés)
            //var service = _nodeUpdateService ?? NodeUpdateServiceLocator.Current;

            if (_nodeUpdateService != null)
            {
                var jsonPropertyName = this.GetJsonPropertyName(propertyExpression);
                _nodeUpdateService.UpdateNode(this, Address, jsonPropertyName, value);
            }
        }

        public FilterNode(NodeNetMessage message) : base(message)
        {
            Update(message);
        }

        public void SetFilterName(int index, string filterName)
        {
            if (index < FilterCount)
            {
                Filters[index] = filterName;
                _nodeUpdateService.UpdateNode(this, Address, $"filterName{index + 1}", filterName);
            }
        }
        public void SetSlotOffset(int index, int offset)
        {
            if (index < FilterCount)
            {
                Offsets[index] = offset;
                _nodeUpdateService.UpdateNode(this, Address, $"offsets{index + 1}", offset);
            }
        }

        public void SetSlotFocuserOffset(int index, int offset)
        {
            if (index < FilterCount)
            {
                FocuserOffsets[index] = offset;
                _nodeUpdateService.UpdateNode(this, Address, $"focuserOffsets{index + 1}", offset);
            }
        }

        public void SetFilterCount(byte count)
        {
            FilterCount = count;
            _nodeUpdateService.UpdateNode(this, Address, $"filter.numberOfFilters", count);
        }
        public override void Update(NodeNetMessage message)
        {
            base.Update(message);
            if (message != null)
            {
                CurrentFilter = message.CurrentFilter.HasValue ? message.CurrentFilter.Value : CurrentFilter;
                CurrentFilterName = message.CurrentFilterName ?? CurrentFilterName;
                FilterCount = message.FilterCount.HasValue ? message.FilterCount.Value : FilterCount;
                HomeOffset = message.homeOffset.HasValue ? message.homeOffset.Value : HomeOffset;
                Filters = message.Filters ?? Filters;
                Offsets = message.Offsets ?? Offsets;
                FocuserOffsets = message.FocuserOffsets ?? FocuserOffsets;
            }
        }

        public void GoToSlot(byte slotNumber)
        {
            var message = new NodeNetMessage
            {
                Command = NodeNetCommands.GoToPosition,
                From = NodeNetAddress.SerialEndpoint,
                To = Address,                
                Value = slotNumber
            };
            _nodeUpdateService.SendCommand(message);
        }

        private byte _currentFilter;
        [JsonProperty("filter")]
        public byte CurrentFilter { get => _currentFilter; set => SetProperty(ref _currentFilter, value); }

        private string _currentFilterName;
        [JsonProperty("filterName")]
        public string CurrentFilterName { get => _currentFilterName; set => SetProperty(ref _currentFilterName, value); }

        private byte _filterCount;
        [JsonProperty("filterCount")]
        public byte FilterCount { get => _filterCount; set => SetProperty(ref _filterCount, value); }

        private string[] _filters;
        [JsonProperty("filters")]
        public string[] Filters { get => _filters; set => SetProperty(ref _filters, value); }


        private int? _homeOffset;
        [JsonProperty("homeOffset")]
        public int? HomeOffset
        {
            get => _homeOffset;
            set
            {
                if(SetProperty(ref _homeOffset, value))
                {
                    SendFilterPropertyUpdate(Address, x => x.HomeOffset, value);
                }
            }
        }

        private int[] _offsets;
        [JsonProperty("offsets")]
        public int[] Offsets { get => _offsets; set => SetProperty(ref _offsets, value); }

        private int[] _focuserOffsets;
        [JsonProperty("focuserOffsets")]
        public int[] FocuserOffsets { get => _focuserOffsets; set => SetProperty(ref _focuserOffsets, value); }

    }

    public enum FilterWheelType
    {
        WFilter5x2 = 5,
        WFilter7x125 = 7
    }
}

namespace BigSisterNodeNet.Core
{
    public partial class NodeNetMessage
    {
        [JsonProperty("filter")]
        public byte? CurrentFilter { get; set; }

        [JsonProperty("filterName")]
        public string? CurrentFilterName { get; set; }

        [JsonProperty("filterCount")]
        public byte? FilterCount { get; set; }

        [JsonProperty("homeOffset")]
        public int? homeOffset { get; set; }


        [JsonProperty("filters")]
        public string[]? Filters;

        [JsonProperty("offsets")]
        public int[]? Offsets;

        [JsonProperty("focuserOffsets")]
        public int[]? FocuserOffsets;
    }
}
