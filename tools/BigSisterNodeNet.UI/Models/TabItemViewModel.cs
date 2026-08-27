using CommunityToolkit.Mvvm.ComponentModel;
using System;
using System.Collections.Generic;
using System.Configuration;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.UI.Models
{
    public partial class TabItemViewModel : ObservableObject
    {
        [ObservableProperty]
        public partial string Header { get; set; }
        
        public ITabViewModel Content { get; set; }

        [ObservableProperty]
        public partial bool IsVisible { get; set; }

        public TabItemViewModel(string header, ITabViewModel content)
        {
            Header = header;
            Content = content;
        }

    }
}
