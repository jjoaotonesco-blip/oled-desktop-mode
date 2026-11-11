using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace OledSettings;

internal static class AppLanguage
{
    internal sealed record Option(string Code, string Name);
    internal static readonly Option[] Options = [new("pt", "Português"), new("en", "English"), new("es", "Español")];
    static readonly string FilePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "OledDesktopMode", "language.txt");
    static ResourceDictionary? dictionary;
    internal static Option Current { get; private set; } = Options[0];
    internal static bool IsConfigured { get; private set; }

    internal static void Load()
    {
        Option? stored = null;
        try { stored = Options.FirstOrDefault(o => o.Code == File.ReadAllText(FilePath).Trim()); }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        IsConfigured = stored != null;
        Apply(stored ?? Options.FirstOrDefault(o => o.Code == CultureInfo.CurrentUICulture.TwoLetterISOLanguageName) ?? Options[1]);
    }

    static void Apply(Option option)
    {
        var dictionaries = Application.Current.Resources.MergedDictionaries;
        var next = new ResourceDictionary { Source = new Uri($"Language.{option.Code}.xaml", UriKind.Relative) };
        if (dictionary != null) dictionaries.Remove(dictionary);
        dictionaries.Add(next);
        dictionary = next;
        Current = option;
    }

    internal static string Text(string key) => (string)Application.Current.FindResource(key);

    internal static void Save(Option option)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        File.WriteAllText(FilePath + ".tmp", option.Code);
        File.Move(FilePath + ".tmp", FilePath, overwrite: true);
        Apply(option);
        IsConfigured = true;
    }

    internal static bool Choose(Window owner)
    {
        var panel = new StackPanel { Margin = new Thickness(24) };
        var dialog = new Window { Owner = owner, Content = panel, Title = "OLED Desktop Mode",
            SizeToContent = SizeToContent.Height, Width = 390, ResizeMode = ResizeMode.NoResize,
            WindowStartupLocation = WindowStartupLocation.CenterOwner, ShowInTaskbar = false };
        panel.Children.Add(new TextBlock { Text = "Idioma / Language", FontSize = 24, Margin = new Thickness(0,0,0,16) });
        var picker = new ComboBox { ItemsSource = Options, SelectedItem = Current, DisplayMemberPath = "Name" };
        panel.Children.Add(picker);
        var confirm = new Button { Content = Text("L40"), IsDefault = true, Margin = new Thickness(0,16,0,0), HorizontalAlignment = HorizontalAlignment.Right };
        panel.Children.Add(confirm);
        picker.SelectionChanged += (_, _) => confirm.Content = ((Option)picker.SelectedItem).Code == "en" ? "Continue" : "Continuar";
        confirm.Click += (_, _) => dialog.DialogResult = true;
        if (dialog.ShowDialog() != true) return false;
        Save((Option)picker.SelectedItem);
        return true;
    }
}
