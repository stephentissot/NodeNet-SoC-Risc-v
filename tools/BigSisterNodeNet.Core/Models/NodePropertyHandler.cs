using BigSisterNodeNet.Core.Extensions;
using BigSisterNodeNet.Core.Services;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Models
{
    public class NodePropertyHandler
    {
        protected readonly INodeUpdateService _nodeUpdateService;

        public NodePropertyHandler()
        {
            _nodeUpdateService = NodeUpdateServiceLocator.Current;
        }

        public event PropertyChangedEventHandler PropertyChanged;
        protected virtual void OnPropertyChanged(
        [CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(
                this,
                new PropertyChangedEventArgs(propertyName));
        }
        protected bool SetProperty<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
        {
            if (field == null)
            {
                // Creating object for the first time, no need to notify
                field = value;
                return false;
            }
            if (EqualityComparer<T>.Default.Equals(field, value))
                return false;

            field = value;
            OnPropertyChanged(propertyName);
            return true;
        }



    }
}
