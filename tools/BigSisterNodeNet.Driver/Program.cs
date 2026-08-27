using BigSisterNodeNet.Core;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.UI;
using System;

namespace BigSisterNodeNet.Driver
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            var app = new App();
            var nodeNet = new NodeNetCore();
            var mainWindow = new MainWindow(nodeNet, HardwareType.Undefinded);

            app.Run(mainWindow);
        }
    }
}
