using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.UI.Models;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Xml.Linq;

namespace BigSisterNodeNet.UI
{
    /// <summary>
    /// Logique d'interaction pour MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {


        NodeNetCore _nodeNet;


        public MainWindow(NodeNetCore nodeNet, HardwareType type)
        {
            _nodeNet = nodeNet;
            InitializeComponent();
            DataContext = new MainViewModel(nodeNet, type);
           
        }

        protected override void OnClosed(EventArgs e)
        {
            _nodeNet.Stop();
            base.OnClosed(e);
        }

    }
}
