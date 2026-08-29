using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.HandleCommands;
using BigSisterNodeNet.Core.Instruments;
using BigSisterNodeNet.Core.Models;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;
using System.Windows.Navigation;
using System.Xml.Linq;

namespace BigSisterNodeNet.UI.Models.Instruments
{
    public partial class FilterNodeViewModel : NodeViewModel
    {
        private FilterNode _filterNode;
        public ObservableCollection<FilterSlotViewModel> Slots { get; } = new();

        public FilterNodeViewModel(FilterNode node) : base(node)
        {
            _filterNode = node;
            RebuildSlots();
        }


        public override void Update(INode node)
        {
            base.Update(node);
            _filterNode = node as FilterNode;
            RebuildSlots();
        }

        private void RebuildSlots()
        {
            Slots.Clear();
            for (byte i = 0; i < _filterNode.FilterCount; i++)
            {
                var slot = new FilterSlotViewModel(i, _filterNode.Filters[i], _filterNode);
                if (i == _filterNode.CurrentFilter - 1) slot.IsCurrent = true;
                Slots.Add(slot);

            }
            _filterType = (FilterWheelType)_filterNode.FilterCount;
        }

        private FilterWheelType _filterType;
        public FilterWheelType FilterType
        {
            get => _filterType;
            set
            {
                if (SetProperty(ref _filterType, value))
                {
                    _filterNode.SetFilterCount((byte)value);
                    RebuildSlots();
                }
            }
        }
        public Array FilterWheelTypes => Enum.GetValues(typeof(FilterWheelType));
    }

    public partial class FilterSlotViewModel : ObservableObject
    {

        private FilterNode _node;
        public byte Index { get; }

        [ObservableProperty]
        public partial bool IsCurrent { get; set; }

        public byte SlotNumber => (byte)(Index + 1);

        private int _focuserOffset;
        public int FocuserOffset
        {
            get => _focuserOffset;
            set
            {
                if (SetProperty(ref _focuserOffset, value))
                {
                    _node.SetSlotFocuserOffset(Index, value);
                }
            }
        }


        private int _offset;
        public int Offset 
        { 
            get => _offset;
            set
            {
                if (SetProperty(ref _offset, value))
                {
                    _node.SetSlotOffset(Index, value);
                }
            }
        }

        private string _name;

        public string Name
        {
            get => _name;
            set
            {
                if (SetProperty(ref _name, value))
                {
                    _node.SetFilterName(Index, value);
                }
            }
        }

        public ICommand GoCommand { get; }

        public FilterSlotViewModel(byte index, string name, FilterNode node)
        {
            Index = index;
            _name = name;
            _node = node;
            _offset = node.Offsets!= null ? node.Offsets[index] : 0;
            _focuserOffset = node.FocuserOffsets != null ? node.FocuserOffsets[index] : 0;
            GoCommand = new RelayCommand(GoToSlot);
        }

        private void GoToSlot()
        {
            _node.GoToSlot((byte)(SlotNumber));
            
        }
    }

}
