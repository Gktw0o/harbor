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
        IslandController? island = null;
        try
        {
            island = new IslandController(host, _window.DispatcherQueue);
        }
        catch (Exception exception)
        {
            // The watchdog matters more than the island: the Shell goes on without it (the Start button then opens
            // Windows' Start).
            _log.Error($"island: couldn't be created: {exception}");
        }
        // The Shell ends with its main window, whatever other windows it has (the island): no Shell runs on without
        // anyone seeing it is there.
        _window.Closed += (_, _) =>
        {
            try
            {
                island?.Dispose();
            }
            catch (Exception exception)
            {
                _log.Error($"island: closing it failed: {exception}");
            }
            finally
            {
                Exit();
            }
        };
        if (_options.TestMode)
        {
            // A test Shell never takes the user's focus, and keeps out of sight: not in Alt+Tab or the taskbar,
            // and not a window Windows can hand the focus to.
            _window.AppWindow.Move(new Windows.Graphics.PointInt32(-32000, -32000));
            _window.AppWindow.IsShownInSwitchers = false;
            Harbor.Platform.Island.TestShellWindow.NeverActivate(WinRT.Interop.WindowNative.GetWindowHandle(_window));
            _window.AppWindow.Show(activateWindow: false);
        }
        else
        {
            _window.Activate();
        }
    }
}
