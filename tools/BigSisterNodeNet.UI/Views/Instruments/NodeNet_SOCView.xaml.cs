using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using BigSisterNodeNet.Core.PlcCore;
using BigSisterNodeNet.UI.Models.Instruments;

namespace BigSisterNodeNet.UI.Views.Instruments
{
    public partial class NodeNet_SOCView : UserControl
    {
        public NodeNet_SOCView()
        {
            InitializeComponent();
        }

        private void TreeViewItem_PreviewMouseRightButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (sender is TreeViewItem item)
            {
                item.IsSelected = true;
                item.Focus();
                e.Handled = false;
                return;
            }

            var dependencyObject = e.OriginalSource as DependencyObject;
            while (dependencyObject != null && !(dependencyObject is TreeViewItem))
            {
                dependencyObject = VisualTreeHelper.GetParent(dependencyObject);
            }

            if (dependencyObject is TreeViewItem treeViewItem)
            {
                treeViewItem.IsSelected = true;
                treeViewItem.Focus();
            }
        }

        private void TreeView_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            if (DataContext is NodeNet_SOCViewModel viewModel && e.NewValue is PointDefinitionTreeNode node)
            {
                viewModel.BrowsePath = node.Path ?? string.Empty;
            }
        }

        private void PointStateValueTextBox_LostFocus(object sender, RoutedEventArgs e)
        {
            if (sender is TextBox textBox && textBox.DataContext is PointStateViewModel pointState)
            {
                pointState.CommitTextEdit();
            }
        }

        private void PointStateValueCheckBox_Click(object sender, RoutedEventArgs e)
        {
            if (sender is CheckBox checkBox && checkBox.DataContext is PointStateViewModel pointState)
            {
                pointState.CommitBoolEdit(checkBox.IsChecked);
            }
        }
    }
}
