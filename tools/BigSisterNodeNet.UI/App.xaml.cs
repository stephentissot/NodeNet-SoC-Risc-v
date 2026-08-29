using MaterialDesignThemes.Wpf;
using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;

namespace BigSisterNodeNet.UI
{
    /// <summary>
    /// Logique d'interaction pour App.xaml
    /// </summary>
    public partial class App : Application
    {
        public App()
        {
            InitializeComponent();
            //ThemeLoader.Initialize(this);
        }
    }

    public static class ThemeLoader
    {
        public static void Initialize(Application app)
        {
            app.Resources.MergedDictionaries.Add(
                new ResourceDictionary
                {
                    Source = new Uri(
                        "pack://application:,,,/MaterialDesignThemes.Wpf;component/Themes/MaterialDesign3.Defaults.xaml")
                });

            var helper = new MaterialDesignThemes.Wpf.PaletteHelper();

            var theme = helper.GetTheme();

            theme.SetBaseTheme(BaseTheme.Dark);
            theme.SetPrimaryColor(Colors.MediumPurple);
            theme.SetSecondaryColor(Colors.Lime);

            helper.SetTheme(theme);
        }
    }

}
