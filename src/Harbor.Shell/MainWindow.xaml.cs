using System.Diagnostics;
using Harbor.Core;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Taskbar;
using Microsoft.UI.Xaml;

namespace Harbor.Shell;

public sealed partial class MainWindow : Window
{
    // Explorer can block SHAppBarMessage; past this, the Shell stops waiting.
    private static readonly TimeSpan RestoreTimeout = TimeSpan.FromSeconds(5);

    private readonly string _pipeName;
    private readonly FileLog _log;
    private readonly TaskbarRestorer _restorer;
    private volatile bool _dockConnected;

    public MainWindow(FileLog log)
    {
        InitializeComponent();
        _log = log;
        _restorer = new TaskbarRestorer(TaskbarTarget.Real, RestoreRecord.PathIn(HarborPaths.DefaultDataDir), log);

        using var process = Process.GetCurrentProcess();
        _pipeName = PipeNames.For(PipeNames.ControlChannel, (uint)process.SessionId);
        StatusText.Text = $"Waiting for the dock on {_pipeName}";

        var server = new ControlPipeServer(_pipeName, TimeSpan.FromSeconds(1), log.Warn);
        server.StatusChanged += status =>
        {
            _dockConnected = status.Connected;
            DispatcherQueue.TryEnqueue(() => StatusText.Text = Describe(status));
        };

        // Never disposed on purpose: it uses no timer or wait handle, and the server may still observe the
        // token after the window closes.
        var shutdown = new CancellationTokenSource();
        Closed += (_, _) => shutdown.Cancel();
        // RunAsync creates the pipe before it first awaits, so a second Shell's task has already failed here.
        var serving = server.RunAsync(shutdown.Token);
        _ = ObserveServerAsync(serving);
        if (!serving.IsCompleted)
        {
            _ = RecoverAtStartupAsync();
        }
    }

    private async Task ObserveServerAsync(Task serving)
    {
        try
        {
            await serving;
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            _log.Error($"couldn't serve {_pipeName}: {exception.Message}");
            StatusText.Text = $"Another process already owns {_pipeName}. Is Harbor Shell already running?";
        }
    }

    // An earlier Harbor run may have died with the taskbar hidden or its auto-hide setting changed. A dock
    // that is already connected restored it when it started, and only the dock touches auto-hide while it runs.
    private async Task RecoverAtStartupAsync()
    {
        if (_dockConnected)
        {
            return;
        }
        var result = await _restorer.RestoreAsync(new RestoreOptions { SessionEnding = TaskbarRestorer.IsSessionEnding }, RestoreTimeout);
        if (result is { AutoHideRestoredTo: { } state, RecordCreatedUtc: { } created })
        {
            TaskbarText.Text = $"Harbor turned the taskbar's auto-hide back {OnOff(state)}, as it was before Harbor hid the taskbar on {created} (UTC).";
        }
    }

    private async void OnRestoreClicked(object sender, RoutedEventArgs e)
    {
        RestoreButton.IsEnabled = false;
        try
        {
            if (_dockConnected)
            {
                var shown = await Task.Run(_restorer.ShowHiddenWindows);
                TaskbarText.Text = shown > 0 ? "The taskbar is back." : "The taskbar is already visible.";
            }
            else
            {
                var result = await _restorer.RestoreAsync(new RestoreOptions(), RestoreTimeout);
                TaskbarText.Text = result switch
                {
                    null => "Explorer isn't responding. If the taskbar stays missing, restart Windows Explorer from Task Manager (Ctrl+Shift+Esc).",
                    { AutoHideRestoredTo: { } state } => $"The taskbar is back, and its auto-hide is {OnOff(state)} again.",
                    { RecordKept: true } => "The taskbar is back, but its auto-hide setting couldn't be restored yet. See the log in %LOCALAPPDATA%\\Harbor\\logs.",
                    { WindowsShown: > 0 } => "The taskbar is back.",
                    _ => "The taskbar is already visible.",
                };
            }
        }
        finally
        {
            RestoreButton.IsEnabled = true;
        }
    }

    private static string OnOff(uint autoHideState) => (autoHideState & 1) != 0 ? "on" : "off";

    private string Describe(ControlLinkStatus status) => status switch
    {
        { Connected: false } => $"Waiting for the dock on {_pipeName}",
        { LastPongSeq: { } seq, RoundTrip: { } roundTrip } => $"Dock connected · pong {seq} · {roundTrip.TotalMilliseconds:0.0} ms",
        _ => "Dock connected",
    };
}
