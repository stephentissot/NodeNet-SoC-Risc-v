using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace BigSisterNodeNet.UI.Models.Instruments
{
    public class InstrumentViewSelector : DataTemplateSelector
    {
        public DataTemplate FilterTemplate { get; set; }
        public DataTemplate FocuserTemplate { get; set; }
        public DataTemplate NodeNetSocTemplate { get; set; }
        public DataTemplate RotatorTemplate { get; set; }
        public DataTemplate EmptyTemplate { get; set; }

        public override DataTemplate SelectTemplate(object item, DependencyObject container)
        {
            switch (item)
            {
                case FilterNodeViewModel:
                    return FilterTemplate;

                case FocuserNodeViewModel:
                    return FocuserTemplate;

                case NodeNet_SOCViewModel:
                    return NodeNetSocTemplate;

                case RotatorNodeViewModel:
                    return RotatorTemplate;
                default:
                    return EmptyTemplate;
            }

            return null;
        }
    }
}
