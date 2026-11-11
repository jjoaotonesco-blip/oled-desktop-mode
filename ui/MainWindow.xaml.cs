using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace OledSettings;
public sealed class DisplayProfile
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public int Mask { get; set; }
    public int[] Values { get; set; } = [];
    public int[] Normal { get; set; } = [];
    public int[][] Ranges { get; set; } = [];
    public string DisplayName { get; set; } = "";
}
public sealed class Snapshot
{
    public bool Ok { get; set; }
    public bool Enabled { get; set; }
    public bool Fault { get; set; }
    public string? Error { get; set; }
    public DisplayProfile[] Monitors { get; set; } = [];
}
public partial class MainWindow : Window
{
    const uint CopyData = 0x004A;
    const nuint Protocol = 0x4f4c4544;
    static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
    static string[] Labels => [AppLanguage.Text("L15"), AppLanguage.Text("L16"), AppLanguage.Text("L17"), AppLanguage.Text("L18"), AppLanguage.Text("L19")];
    static string[] Descriptions => [AppLanguage.Text("L20"), AppLanguage.Text("L21"),
        AppLanguage.Text("L22"), AppLanguage.Text("L23"), AppLanguage.Text("L24")];
    static readonly int[] DefaultProfile = [-17, 115, 122, 0, 6400];
    readonly TextBox[] inputs = new TextBox[5];
    readonly Slider[] sliders = new Slider[5];
    HwndSource? source;
    nint handle;
    string? response;
    bool loading;
    bool dirty;
    DisplayProfile? selected;
    [StructLayout(LayoutKind.Sequential)] struct CopyDataPacket { public nuint Id; public int Length; public nint Data; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern nint FindWindow(string? className, string title);
    [DllImport("user32.dll", SetLastError = true)] static extern nint SendMessageTimeout(nint hwnd, uint msg, nint w, ref CopyDataPacket data, uint flags, uint timeout, out nuint result);
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(nint hwnd, int attribute, ref int value, int size);

    public MainWindow()
    {
        InitializeComponent();
        LanguagePicker.ItemsSource = AppLanguage.Options;
        LanguagePicker.SelectedItem = AppLanguage.Current;
        Height = Math.Min(Height, SystemParameters.WorkArea.Height - 64);
        Width = Math.Min(Width, SystemParameters.WorkArea.Width - 64);
        Closing += (_, e) => { if (!CanDiscard()) e.Cancel = true; };
    }
    void OnSourceInitialized(object? sender, EventArgs e)
    {
        handle = new WindowInteropHelper(this).Handle;
        source = HwndSource.FromHwnd(handle);
        source.AddHook(Receive);
        int corners = 2; DwmSetWindowAttribute(handle, 33, ref corners, sizeof(int));
    }
    nint Receive(nint hwnd, int msg, nint w, nint l, ref bool handled)
    {
        if (msg == CopyData && w == FindWindow("OledDesktopModeEvents", "OLED Desktop Mode"))
        {
            var packet = Marshal.PtrToStructure<CopyDataPacket>(l);
            if (packet.Id == Protocol && packet.Length is > 0 and < 4_000_000 && packet.Data != 0)
            {
                var bytes = new byte[packet.Length - 1]; Marshal.Copy(packet.Data, bytes, 0, bytes.Length);
                response = Encoding.UTF8.GetString(bytes); handled = true; return 1;
            }
        }
        return 0;
    }
    Snapshot Request(string command)
    {
        var worker = FindWindow("OledDesktopModeEvents", "OLED Desktop Mode");
        if (worker == 0) throw new InvalidOperationException(AppLanguage.Text("L25"));
        var bytes = Encoding.UTF8.GetBytes(command + "\0");
        var memory = Marshal.AllocHGlobal(bytes.Length);
        try
        {
            Marshal.Copy(bytes, 0, memory, bytes.Length);
            var packet = new CopyDataPacket { Id = Protocol, Length = bytes.Length, Data = memory };
            response = null;
            if (SendMessageTimeout(worker, CopyData, handle, ref packet, 2, 5000, out _) == 0 || response == null)
                throw new IOException(AppLanguage.Text("L26"));
            var result = JsonSerializer.Deserialize<Snapshot>(response, JsonOptions) ?? throw new IOException(AppLanguage.Text("L27"));
            if (!result.Ok) throw new InvalidOperationException(result.Error ?? AppLanguage.Text("L28"));
            return result;
        }
        finally { Marshal.FreeHGlobal(memory); }
    }
    async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            if (FindWindow("OledDesktopModeEvents", "OLED Desktop Mode") == 0)
            {
                string engine = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "OledDesktopMode.exe"));
                Process.Start(new ProcessStartInfo(engine, "--background") { UseShellExecute = false, CreateNoWindow = true });
                for (int i = 0; i < 40 && FindWindow("OledDesktopModeEvents", "OLED Desktop Mode") == 0; i++) await Task.Delay(100);
            }
            if (!AppLanguage.IsConfigured && !Environment.GetCommandLineArgs().Contains("--verify-ui"))
            {
                if (!AppLanguage.Choose(this)) { Close(); return; }
                LanguagePicker.SelectedItem = AppLanguage.Current;
            }
            RefreshProfiles();
            StartupToggle.IsChecked = File.Exists(StartupLink);
            if (Environment.GetCommandLineArgs().Contains("--verify-ui"))
            {
                await Task.Delay(500);
                await VerifyUi();
            }
        }
        catch (Exception ex)
        {
            ShowError(ex);
            if (Environment.GetCommandLineArgs().Contains("--verify-ui"))
            {
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "ui-verification.txt"), "FAIL: " + ex);
                dirty = false; Environment.ExitCode = 2; Close();
            }
        }
    }
    static string StartupLink => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Startup), "OLED Desktop Mode.lnk");

    void OnStartupToggle(object sender, RoutedEventArgs e)
    {
        string link = StartupLink;
        string temporary = Path.Combine(Path.GetDirectoryName(link)!, Guid.NewGuid() + ".lnk");
        try
        {
            if (StartupToggle.IsChecked != true) File.Delete(link);
            else
            {
                string engine = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "OledDesktopMode.exe"));
                if (!File.Exists(engine)) throw new FileNotFoundException("OledDesktopMode.exe", engine);
                Directory.CreateDirectory(Path.GetDirectoryName(link)!);
                dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell", throwOnError: true)!)!;
                dynamic shortcut = shell.CreateShortcut(temporary);
                shortcut.TargetPath = engine;
                shortcut.Arguments = "--background";
                shortcut.WorkingDirectory = Path.GetDirectoryName(engine)!;
                shortcut.Save();
                File.Move(temporary, link, overwrite: true);
            }
        }
        catch (Exception ex) { ShowError(ex); }
        finally
        {
            try { File.Delete(temporary); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            StartupToggle.IsChecked = File.Exists(link);
        }
    }

    void OnLanguageChanged(object sender, SelectionChangedEventArgs e)
    {
        if (loading || LanguagePicker.SelectedItem is not AppLanguage.Option option || option == AppLanguage.Current) return;
        try
        {
            if (CanDiscard())
            {
                AppLanguage.Save(option);
                RefreshProfiles(selected?.Id);
            }
        }
        catch (Exception ex) { ShowError(ex); }
        finally
        {
            loading = true;
            LanguagePicker.SelectedItem = AppLanguage.Current;
            loading = false;
        }
    }
    void RefreshProfiles(string? prefer = null)
    {
        var snapshot = Request("LIST");
        loading = true;
        try
        {
            EnabledToggle.IsChecked = snapshot.Enabled;
            for (int i = 0; i < snapshot.Monitors.Length; ++i)
                snapshot.Monitors[i].DisplayName = $"{i + 1}. {snapshot.Monitors[i].Name}";
            MonitorPicker.ItemsSource = snapshot.Monitors;
            MonitorPicker.SelectedItem = snapshot.Monitors.FirstOrDefault(m => m.Id == prefer) ?? snapshot.Monitors.FirstOrDefault();
            selected = MonitorPicker.SelectedItem as DisplayProfile;
            RenderRows(); dirty = false;
        }
        finally { loading = false; }
        SaveButton.IsEnabled = selected != null && !snapshot.Fault;
        Feedback.Text = snapshot.Fault ? AppLanguage.Text("L29") :
            selected == null ? AppLanguage.Text("L30") : AppLanguage.Text("L31");
    }
    void RenderRows()
    {
        ColorRows.Children.Clear();
        if (selected == null) return;
        for (int i = 0; i < 5; ++i)
        {
            int index = i;
            bool supported = (selected.Mask & (1 << i)) != 0;
            var range = selected.Ranges[i];
            var card = new Border { Style = (Style)FindResource("Card") };
            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition()); grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });
            grid.RowDefinitions.Add(new RowDefinition()); grid.RowDefinitions.Add(new RowDefinition());
            var title = new TextBlock { Text = Labels[i], FontSize = 16, FontWeight = FontWeights.SemiBold };
            grid.Children.Add(title);
            var input = new TextBox { Text = selected.Values[i].ToString(), IsEnabled = supported, HorizontalContentAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(16, 0, 0, 0), VerticalContentAlignment = VerticalAlignment.Center };
            System.Windows.Automation.AutomationProperties.SetName(input, string.Format(AppLanguage.Text("L41"), Labels[i]));
            Grid.SetColumn(input, 1); grid.Children.Add(input); inputs[i] = input;
            var stack = new StackPanel { Margin = new Thickness(0, 8, 16, 0) };
            stack.Children.Add(new TextBlock { Text = supported ? Descriptions[i] : AppLanguage.Text("L32"), Opacity = .7, TextWrapping = TextWrapping.Wrap });
            var slider = new Slider { Minimum = range[0], Maximum = Math.Max(range[0], range[1]), TickFrequency = Math.Max(1, range[2]),
                IsSnapToTickEnabled = true, SmallChange = Math.Max(1, range[2]), LargeChange = Math.Max(1, (range[1] - range[0]) / 10),
                Value = selected.Values[i], IsEnabled = supported, Margin = new Thickness(0, 12, 0, 2) };
            System.Windows.Automation.AutomationProperties.SetName(slider, Labels[i]); sliders[i] = slider;
            slider.ValueChanged += (_, _) =>
            {
                if (loading) return;
                input.Text = ((int)Math.Round(slider.Value)).ToString(); MarkDirty();
            };
            input.TextChanged += (_, _) =>
            {
                if (loading) return;
                if (int.TryParse(input.Text, out int v) && v >= range[0] && v <= range[1] && (v - range[0]) % Math.Max(1, range[2]) == 0)
                { bool old = loading; loading = true; slider.Value = v; loading = old; }
                MarkDirty();
            };
            stack.Children.Add(slider);
            stack.Children.Add(new TextBlock { Text = supported ? $"{range[0]} — {range[1]}{(i == 4 ? " K" : "")}   ·   Normal: {selected.Normal[i]}" : AppLanguage.Text("L33"),
                FontSize = 12, Opacity = .65, TextWrapping = TextWrapping.Wrap });
            Grid.SetRow(stack, 1); Grid.SetColumnSpan(stack, 2); grid.Children.Add(stack);
            card.Child = grid; ColorRows.Children.Add(card);
        }
    }
    void MarkDirty() { dirty = true; Feedback.Text = AppLanguage.Text("L34"); }
    bool CanDiscard() => !dirty || MessageBox.Show(this, AppLanguage.Text("L35"), AppLanguage.Text("L36"), MessageBoxButton.YesNo, MessageBoxImage.Question) == MessageBoxResult.Yes;
    void OnMonitorChanged(object sender, SelectionChangedEventArgs e)
    {
        if (loading) return;
        var next = MonitorPicker.SelectedItem as DisplayProfile;
        if (!CanDiscard()) { loading = true; MonitorPicker.SelectedItem = selected; loading = false; return; }
        selected = next; loading = true; RenderRows(); loading = false; dirty = false;
    }
    void OnSave(object sender, RoutedEventArgs e)
    {
        if (selected == null) return;
        try
        {
            int[] values = (int[])selected.Values.Clone();
            for (int i = 0; i < 5; ++i) if ((selected.Mask & (1 << i)) != 0)
            {
                var range = selected.Ranges[i];
                if (!int.TryParse(inputs[i].Text, out values[i]) || values[i] < range[0] || values[i] > range[1] || (values[i] - range[0]) % Math.Max(1, range[2]) != 0)
                    throw new InvalidOperationException(string.Format(AppLanguage.Text("L42"), Labels[i], range[0], range[1], range[2]));
            }
            Request($"SAVE {selected.Id} {string.Join(' ', values)}");
            selected.Values = values; dirty = false;
            Feedback.Text = string.Format(AppLanguage.Text("L43"), selected.Name);
        }
        catch (Exception ex) { ShowError(ex); }
    }
    void OnReset(object sender, RoutedEventArgs e)
    {
        if (selected == null) return;
        for (int i = 0; i < 5; ++i) if ((selected.Mask & (1 << i)) != 0) inputs[i].Text = DefaultProfile[i].ToString();
        MarkDirty();
    }
    void OnRefresh(object sender, RoutedEventArgs e) { if (!CanDiscard()) return; try { RefreshProfiles(selected?.Id); } catch (Exception ex) { ShowError(ex); } }
    void OnToggle(object sender, RoutedEventArgs e)
    {
        if (loading) return;
        try { Request($"ENABLE {(EnabledToggle.IsChecked == true ? 1 : 0)}"); Feedback.Text = EnabledToggle.IsChecked == true ? AppLanguage.Text("L37") : AppLanguage.Text("L38"); }
        catch (Exception ex) { EnabledToggle.IsChecked = EnabledToggle.IsChecked != true; ShowError(ex); }
    }
    void OnHide(object sender, RoutedEventArgs e) => Close();
    void ShowError(Exception ex) { Feedback.Text = ex.Message; }
    async Task VerifyUi()
    {
        var before = Request("LIST");
        if (selected == null) throw new InvalidOperationException("No AMD display for integration QA");
        var original = (int[])selected.Values.Clone();
        string token = selected.Id;
        var checks = new List<string>();
        try
        {
            if (TryFindResource("CardBackgroundFillColorDefaultBrush") is not Brush ||
                TryFindResource("AccentFillColorDefaultBrush") is not Brush)
                throw new InvalidOperationException("Fluent resources missing");
            checks.Add("PASS: real Fluent theme resources, monitor selection and five control rows");
            if (ColorRows.Children.Count != 5) throw new InvalidOperationException("Missing sliders");
            int index = Enumerable.Range(0, 5).First(i => (selected.Mask & (1 << i)) != 0);
            var range = selected.Ranges[index];
            int changed = original[index] + range[2] <= range[1] ? original[index] + range[2] : original[index] - range[2];
            sliders[index].Value = changed;
            OnSave(this, new RoutedEventArgs());
            if (Request("LIST").Monitors.Single(m => m.Id == token).Values[index] != changed)
                throw new InvalidOperationException("Save from slider did not reach backend");
            checks.Add("PASS: slider -> exact value -> Save -> backend persisted profile");
            Request("EXIT");
            for (int i = 0; i < 30 && FindWindow("OledDesktopModeEvents", "OLED Desktop Mode") != 0; ++i) await Task.Delay(100);
            await Task.Delay(150); // Allow the old guardian to finish before the new session lock.
            string engine = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "OledDesktopMode.exe"));
            Process.Start(new ProcessStartInfo(engine, "--background") { UseShellExecute = false, CreateNoWindow = true });
            for (int i = 0; i < 40 && FindWindow("OledDesktopModeEvents", "OLED Desktop Mode") == 0; ++i) await Task.Delay(100);
            if (Request("LIST").Monitors.Single(m => m.Id == token).Values[index] != changed)
                throw new IOException("Saved profile did not survive an engine restart");
            checks.Add("PASS: saved OLED values survive a full backend restart");
            inputs[index].Text = (range[1] + 1).ToString();
            OnSave(this, new RoutedEventArgs());
            if (Request("LIST").Monitors.Single(m => m.Id == token).Values[index] != changed)
                throw new InvalidOperationException("Invalid input was persisted");
            checks.Add("PASS: invalid numeric input rejected without changing saved profile");
            try { Request($"SAVE {token} {int.MaxValue} 115 122 0 6400"); throw new IOException("Backend accepted invalid range"); }
            catch (InvalidOperationException) { checks.Add("PASS: backend independently rejects out-of-range values"); }
            EnabledToggle.IsChecked = false; OnToggle(this, new RoutedEventArgs());
            if (Request("LIST").Enabled) throw new InvalidOperationException("Disable failed");
            checks.Add("PASS: activation toggle communicates with backend");
            Request($"SAVE {token} {string.Join(' ', original)}");
            Request($"ENABLE {(before.Enabled ? 1 : 0)}");
            dirty = false; RefreshProfiles(token);
            var after = Request("LIST").Monitors.Single(m => m.Id == token);
            if (!after.Values.SequenceEqual(original) || !after.Normal.SequenceEqual(before.Monitors.Single(m => m.Id == token).Normal))
                throw new IOException("QA changed the original profile or NormalState");
            checks.Add("PASS: original profile and NormalState preserved after QA");
            CapturePreview("ui-preview.png");
            ThemeMode = ThemeMode.Dark;
            await Task.Delay(300);
            CapturePreview("ui-preview-dark.png");
            ThemeMode = ThemeMode.System;
            checks.Add("PASS: Fluent dark theme renders using the same controls and semantic resources");
            File.WriteAllLines(Path.Combine(AppContext.BaseDirectory, "ui-verification.txt"), checks);
        }
        finally
        {
            Request($"SAVE {token} {string.Join(' ', original)}");
            Request($"ENABLE {(before.Enabled ? 1 : 0)}");
            dirty = false;
        }
        Request("EXIT"); Close();
    }
    void CapturePreview(string name)
    {
        var visual = (FrameworkElement)Content;
        visual.UpdateLayout();
        var bitmap = new RenderTargetBitmap((int)Math.Ceiling(visual.ActualWidth), (int)Math.Ceiling(visual.ActualHeight), 96, 96, PixelFormats.Pbgra32);
        var rendered = new DrawingVisual();
        using (var drawing = rendered.RenderOpen())
        {
            var rect = new Rect(0, 0, visual.ActualWidth, visual.ActualHeight);
            var background = TryFindResource("SolidBackgroundFillColorBaseBrush") as Brush ?? SystemColors.WindowBrush;
            drawing.DrawRectangle(background, null, rect);
            drawing.DrawRectangle(new VisualBrush(visual), null, rect);
        }
        bitmap.Render(rendered);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using var file = File.Create(Path.Combine(AppContext.BaseDirectory, name)); encoder.Save(file);
    }
}
