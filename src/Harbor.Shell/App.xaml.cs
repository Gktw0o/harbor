using System.Runtime.InteropServices;
using Harbor.Core;
using Harbor.Core.Logging;
using Microsoft.UI.Xaml;

namespace Harbor.Shell;

public partial class App : Application
{
    private readonly ShellOptions _options;
    private readonly FileLog _log;
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
        // A test Shell (docs/adr/0013) has its own data dir, so its log goes there too.
        var arguments = Environment.GetCommandLineArgs()[1..];
        var (options, error) = ShellOptions.Parse(arguments);
        _options = options ?? ShellOptions.Normal;
        // A refused command line is logged where it pointed, never into the user's own log.
        _log = new FileLog(HarborPaths.LogsDir(options?.DataDirOrDefault ?? ShellOptions.DataDirNamedIn(arguments) ?? HarborPaths.DefaultDataDir), "shell");
        UnhandledException += (_, e) => _log.Error($"unhandled exception: {e.Exception}");
        if (error is not null)
        {
            _log.Error($"invalid command line: {error}; exiting");
            Environment.Exit(2);
        }
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _log.Info($"Harbor Shell starting: {RuntimeInformation.ProcessArchitecture} build, process {Environment.ProcessId}" +
                  (_options.TestMode ? ", test mode" : ""));
        var host = new ShellHost(_options, _log, Path.Combine(AppContext.BaseDirectory, "harbor-dock.exe"));
        _window = new MainWindow(host);
        // The Shell ends with its main window, whatever other windows it has (the island, M4): no Shell runs on
        // without anyone seeing it is there.
        _window.Closed += (_, _) => Exit();
        if (_options.TestMode)
        {
            // A test Shell never takes the user's focus, and keeps out of sight.
            _window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            _window.AppWindow.Show(activateWindow: false);
        }
        else
        {
            _window.Activate();
        }
    }
}
