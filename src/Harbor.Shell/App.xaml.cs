using System.Runtime.InteropServices;
using Harbor.Core;
using Harbor.Core.Logging;
using Microsoft.UI.Xaml;

namespace Harbor.Shell;

public partial class App : Application
{
    private readonly FileLog _log = new(HarborPaths.LogsDir(HarborPaths.DefaultDataDir), "shell");
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
        UnhandledException += (_, e) => _log.Error($"unhandled exception: {e.Exception}");
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _log.Info($"Harbor Shell starting: {RuntimeInformation.ProcessArchitecture} build, process {Environment.ProcessId}");
        _window = new MainWindow(_log);
        _window.Activate();
    }
}
