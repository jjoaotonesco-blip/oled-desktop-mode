using System.Windows;
namespace OledSettings;
public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        AppLanguage.Load();
        base.OnStartup(e);
    }
}
