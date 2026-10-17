using Harbor.Core.Apps;
using Harbor.Core.Ipc;
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

    public IslandPanel(AppIndex apps)
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

        // The sections come with M4 steps 13-15.
        _sections = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12, HorizontalAlignment = HorizontalAlignment.Center };
        foreach (var name in new[] { "Stats", "Power", "Media", "Controls" })
        {
            var card = new Border
            {
                Width = 200,
                Height = 250,
                CornerRadius = new CornerRadius(16),
                Background = new SolidColorBrush(Windows.UI.Color.FromArgb(40, 128, 128, 128)),
                Child = new TextBlock { Text = name, Margin = new Thickness(14), FontSize = 15 },
            };
            AutomationProperties.SetName(card, name);
            _sections.Children.Add(card);
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

    /// <summary>The island opened: a fresh search.</summary>
    public void Reset()
    {
        Search.Text = "";
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
