using System.Globalization;
using Harbor.Core.Apps;
using Harbor.Core.Ipc;
using Harbor.Core.Power;
using Harbor.Core.Stats;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;

namespace Harbor.Shell;

// The open island (docs/adr/0013, 0016): a search box that finds apps as the user types (Enter or a click starts
// one), Windows' Start one step away, and the sections that M4's later steps fill. On the UI thread only.
internal sealed partial class IslandPanel
{
    private const int MaxResults = 8;
    private readonly AppIndex _apps;
    private readonly ListView _results;
    private readonly StackPanel _sections;
    private readonly StackPanel _stats;
    private StackPanel? _confirmRow;
    private TextBlock? _confirmText;
    private Button? _confirmButton;
    private PowerAction? _confirming;

    public IslandPanel(AppIndex apps, PowerCapabilities power)
    {
        _apps = apps;
        Search = new TextBox { PlaceholderText = "Search apps", Width = 420, HorizontalAlignment = HorizontalAlignment.Center };
        AutomationProperties.SetName(Search, "Search apps");
        Search.TextChanged += (_, _) => ShowResults();
        Search.KeyDown += OnSearchKeyDown;

        _results = new ListView { SelectionMode = ListViewSelectionMode.Single, IsItemClickEnabled = true, Width = 420, MaxHeight = 300, HorizontalAlignment = HorizontalAlignment.Center, Visibility = Visibility.Collapsed };
        AutomationProperties.SetName(_results, "Apps found");
        _results.ItemClick += (_, e) =>
        {
            if (e.ClickedItem is ListViewItem { Tag: AppTarget target })
            {
                LaunchRequested?.Invoke(target);
            }
        };

        _sections = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12, HorizontalAlignment = HorizontalAlignment.Center };
        _stats = new StackPanel { Spacing = 6 };
        _sections.Children.Add(Card("Stats", _stats));
        _sections.Children.Add(Card("Power", PowerSection(power)));
        // The Media and Controls sections come with M4 steps 14-15.
        foreach (var name in new[] { "Media", "Controls" })
        {
            _sections.Children.Add(Card(name, new StackPanel()));
        }

        var windowsStart = new HyperlinkButton { Content = "Windows Start", HorizontalAlignment = HorizontalAlignment.Center };
        windowsStart.Click += (_, _) => WindowsStartRequested?.Invoke();

        var layout = new StackPanel { Spacing = 14, Margin = new Thickness(0, 22, 0, 0), HorizontalAlignment = HorizontalAlignment.Center };
        layout.Children.Add(Search);
        layout.Children.Add(_results);
        layout.Children.Add(_sections);
        layout.Children.Add(windowsStart);
        Root = layout;
    }

    public UIElement Root { get; }

    /// <summary>Where the focus goes when the island opens: typing starts a search at once.</summary>
    public TextBox Search { get; }

    /// <summary>An app was chosen.</summary>
    public event Action<AppTarget>? LaunchRequested;

    /// <summary>"Windows Start" was chosen.</summary>
    public event Action? WindowsStartRequested;

    /// <summary>A power action was chosen (and confirmed, where it asks).</summary>
    public event Action<PowerAction>? PowerRequested;

    /// <summary>The latest stats (docs/adr/0016); parts that aren't available aren't shown.</summary>
    public void ShowStats(StatsReading reading)
    {
        ArgumentNullException.ThrowIfNull(reading);
        var culture = CultureInfo.CurrentCulture;
        var lines = new List<string>();
        if (reading.Cpu is { } cpu)
        {
            lines.Add(reading.Cores.Count > 0
                ? string.Format(culture, "CPU {0:0} % · {1} cores", cpu, reading.Cores.Count)
                : string.Format(culture, "CPU {0:0} %", cpu));
        }
        if (reading is { MemoryUsed: { } used, MemoryTotal: { } total })
        {
            lines.Add($"Memory {StatsMath.Size(used, culture)} of {StatsMath.Size(total, culture)}");
        }
        if (reading.Gpu is { } gpu)
        {
            lines.Add(string.Format(culture, "GPU {0:0} %", gpu));
        }
        if (reading.Battery is { } battery)
        {
            lines.Add(battery switch
            {
                { Charging: true } => string.Format(culture, "Battery {0} %, charging", battery.Percent),
                { TimeLeft: { } left } => string.Format(culture, "Battery {0} %, {1} left", battery.Percent, StatsMath.Duration(left)),
                _ => string.Format(culture, "Battery {0} %", battery.Percent),
            });
        }
        foreach (var drive in reading.Drives.Take(3))
        {
            lines.Add($"{drive.Name} {StatsMath.Size(drive.Free, culture)} free of {StatsMath.Size(drive.Total, culture)}");
        }
        _stats.Children.Clear();
        foreach (var line in lines)
        {
            _stats.Children.Add(new TextBlock { Text = line, TextWrapping = TextWrapping.Wrap });
        }
    }

    // Lock, sign out, sleep, hibernate (if enabled), restart, shut down. Those that end the session or the PC ask
    // first, in the section itself: a second button names what will happen.
    private StackPanel PowerSection(PowerCapabilities power)
    {
        var section = new StackPanel { Spacing = 4 };
        foreach (var action in PowerPolicy.Offered(power))
        {
            var button = new Button { Content = PowerPolicy.Label(action), HorizontalAlignment = HorizontalAlignment.Stretch };
            button.Click += (_, _) => Choose(action);
            section.Children.Add(button);
        }
        _confirmText = new TextBlock { TextWrapping = TextWrapping.Wrap };
        _confirmButton = new Button();
        _confirmButton.Click += (_, _) =>
        {
            if (_confirming is { } action)
            {
                HideConfirmation();
                PowerRequested?.Invoke(action);
            }
        };
        var cancel = new Button { Content = "Cancel" };
        cancel.Click += (_, _) => HideConfirmation();
        _confirmRow = new StackPanel { Spacing = 4, Visibility = Visibility.Collapsed, Children = { _confirmText, new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, Children = { _confirmButton, cancel } } } };
        section.Children.Add(_confirmRow);
        return section;
    }

    private void Choose(PowerAction action)
    {
        if (!PowerPolicy.NeedsConfirmation(action))
        {
            PowerRequested?.Invoke(action);
            return;
        }
        _confirming = action;
        _confirmText!.Text = $"{PowerPolicy.Label(action)} now? Unsaved work may be lost.";
        _confirmButton!.Content = PowerPolicy.Label(action);
        AutomationProperties.SetName(_confirmButton, $"{PowerPolicy.Label(action)} now");
        _confirmRow!.Visibility = Visibility.Visible;
        _confirmButton.Focus(FocusState.Programmatic);
    }

    private void HideConfirmation()
    {
        _confirming = null;
        _confirmRow!.Visibility = Visibility.Collapsed;
    }

    private static Border Card(string name, UIElement content)
    {
        var card = new Border
        {
            Width = 200,
            MinHeight = 250,
            CornerRadius = new CornerRadius(16),
            Padding = new Thickness(14),
            Background = new SolidColorBrush(Windows.UI.Color.FromArgb(40, 128, 128, 128)),
            Child = new StackPanel { Spacing = 10, Children = { new TextBlock { Text = name, FontSize = 15 }, content } },
        };
        AutomationProperties.SetName(card, name);
        return card;
    }

    /// <summary>The island opened: a fresh search, and nothing left waiting for confirmation.</summary>
    public void Reset()
    {
        Search.Text = "";
        HideConfirmation();
        ShowResults();
    }

    /// <summary>A new list of apps arrived: the same query, new results.</summary>
    public void Refresh() => ShowResults();

    private void ShowResults()
    {
        var found = _apps.Search.Find(Search.Text, MaxResults);
        _results.Items.Clear();
        foreach (var app in found)
        {
            _results.Items.Add(new ListViewItem { Content = app.Name, Tag = app.Target });
        }
        var searching = Search.Text.Trim().Length > 0;
        _results.Visibility = searching && found.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
        _sections.Visibility = searching ? Visibility.Collapsed : Visibility.Visible;
        _results.SelectedIndex = found.Count > 0 ? 0 : -1;
    }

    // Enter starts the chosen app (the first one unless the arrows moved), the arrows choose; the focus stays in
    // the box, so typing goes on.
    private void OnSearchKeyDown(object sender, KeyRoutedEventArgs e)
    {
        var count = _results.Items.Count;
        switch (e.Key)
        {
            case Windows.System.VirtualKey.Enter when count > 0:
                e.Handled = true;
                if ((_results.SelectedItem ?? _results.Items[0]) is ListViewItem { Tag: AppTarget target })
                {
                    LaunchRequested?.Invoke(target);
                }
                break;
            case Windows.System.VirtualKey.Down when count > 0:
                e.Handled = true;
                _results.SelectedIndex = Math.Min(_results.SelectedIndex + 1, count - 1);
                break;
            case Windows.System.VirtualKey.Up when count > 0:
                e.Handled = true;
                _results.SelectedIndex = Math.Max(_results.SelectedIndex - 1, 0);
                break;
        }
    }
}
