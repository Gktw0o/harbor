using System.Diagnostics;
using Harbor.Core;
using Harbor.Core.Ipc;
using Harbor.Core.Logging;
using Harbor.Core.Settings;
using Harbor.Core.Supervision;
using Harbor.Core.Taskbar;
using Microsoft.UI.Xaml;

namespace Harbor.Shell;

// Harbor Shell's status window: shows what the host reports and calls its commands. Closing it ends the Shell.
public sealed partial class MainWindow : Window
{
    private readonly ShellHost _host;
    private readonly ILog _log;
    private readonly DockSupervisor _supervisor;
    private readonly SettingsFile _settingsFile;
    private bool _updatingToggle;

    public MainWindow(ShellHost host)
    {
        InitializeComponent();
        _host = host;
        _log = host.Log;
        _supervisor = host.Supervisor;
        _settingsFile = host.Settings;
        if (host.Options.TestMode)
        {
            Title = "Harbor Shell (test)";
        }
        StatusText.Text = $"Waiting for the dock on {host.PipeName}";

        _supervisor.LinkStatusChanged += status => DispatcherQueue.TryEnqueue(() => StatusText.Text = Describe(status));
        _supervisor.DockStatusChanged += status => DispatcherQueue.TryEnqueue(() => ShowDockStatus(status));
        _supervisor.TaskbarRestored += restore => DispatcherQueue.TryEnqueue(() => TaskbarText.Text = Describe(restore));
        _supervisor.TaskbarStateChanged += state => DispatcherQueue.TryEnqueue(() => TaskbarText.Text = Describe(state, HideToggle.IsOn));
        _supervisor.HideRequestChanged += hidden => DispatcherQueue.TryEnqueue(() =>
        {
            // Turned off by the watchdog (panic hotkey, dock ended...): show that without echoing it back.
            _updatingToggle = true;
            HideToggle.IsOn = hidden;
            _updatingToggle = false;
        });
        host.SettingsProblemChanged += problem => DispatcherQueue.TryEnqueue(() => ShowSettingsProblem(problem));
        host.PinnedChanged += items => DispatcherQueue.TryEnqueue(() => ShowPinned(items));
        host.PinRequestHandled += (request, result) => DispatcherQueue.TryEnqueue(() => ShowPinRequest(request, result));

        // Never disposed on purpose: it uses no timer or wait handle, and the host may still observe the token
        // after the window closes.
        var shutdown = new CancellationTokenSource();
        Closed += (_, _) => shutdown.Cancel();
        _ = RunHostAsync(shutdown.Token);
    }

    private async Task RunHostAsync(CancellationToken cancellationToken)
    {
        try
        {
            await _host.RunAsync(cancellationToken);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            _log.Error($"couldn't serve {_host.PipeName}: {exception.Message}");
            DockText.Text = $"Another process already owns {_host.PipeName}. Is Harbor Shell already running?";
            RestoreButton.IsEnabled = false;
        }
    }

    private void ShowDockStatus(DockStatus status)
    {
        DockText.Text = status switch
        {
            { State: DockState.Running, ProcessId: { } id } => $"Dock running (process {id}). Harbor Shell restores the taskbar if it stops.",
            { State: DockState.Stopped, Reason: { } reason, Next: { } next } => $"The dock stopped: {reason}. {Sentence(next)}.",
            { State: DockState.Stopped, Reason: { } reason } => $"The dock stopped: {reason}.",
            { Reason: { } reason } => reason,
            _ => "Waiting for the dock…",
        };
        StartDockButton.IsEnabled = status.State != DockState.Running;
        StopDockButton.IsEnabled = status.State == DockState.Running;
    }

    private static string Sentence(string text) => text.Length == 0 ? text : char.ToUpperInvariant(text[0]) + text[1..];

    private void ShowPinned(DockItems items) =>
        PinnedText.Text = items.Items.Count == 0
            ? "None. Add some with \"Edit pinned apps…\"."
            : string.Join("\n", items.Items.Select(item => item.Name));

    // A refused request is shown (it may need the user to fix settings.json); one that worked shows in the list.
    private void ShowPinRequest(PinRequest request, SettingsChangeResult result)
    {
        PinRequestText.Text = result.Written
            ? ""
            : $"Couldn't {(request.Action == PinAction.Pin ? "pin" : "unpin")} that app: {result.Outcome}.";
        PinRequestText.Visibility = result.Written ? Visibility.Collapsed : Visibility.Visible;
    }

    private void ShowSettingsProblem(string? problem)
    {
        SettingsProblemText.Text = problem is null ? "" : $"settings.json isn't used: {problem}. The last good pinned apps stay on the dock until it's fixed.";
        SettingsProblemText.Visibility = problem is null ? Visibility.Collapsed : Visibility.Visible;
    }

    private void OnStopDockClicked(object sender, RoutedEventArgs e)
    {
        StopDockButton.IsEnabled = false;
        _supervisor.StopDock();
    }

    // In the user's editor for .json files, or Notepad if there is none.
    private void OnEditPinnedClicked(object sender, RoutedEventArgs e)
    {
        try
        {
            Process.Start(new ProcessStartInfo(_settingsFile.FilePath) { UseShellExecute = true })?.Dispose();
        }
        catch (System.ComponentModel.Win32Exception)
        {
            try
            {
                Process.Start("notepad.exe", $"\"{_settingsFile.FilePath}\"")?.Dispose();
            }
            catch (System.ComponentModel.Win32Exception exception)
            {
                _log.Warn($"settings: can't open {_settingsFile.FilePath}: {exception.Message}");
            }
        }
    }

    private async void OnRestoreClicked(object sender, RoutedEventArgs e)
    {
        RestoreButton.IsEnabled = false;
        try
        {
            TaskbarText.Text = Describe(new TaskbarRestore("you asked", await _supervisor.RestoreTaskbarAsync()));
        }
        finally
        {
            RestoreButton.IsEnabled = true;
        }
    }

    private void OnStartDockClicked(object sender, RoutedEventArgs e)
    {
        StartDockButton.IsEnabled = false;
        _supervisor.StartDock();
    }

    private void OnHideToggled(object sender, RoutedEventArgs e)
    {
        if (!_updatingToggle)
        {
            _supervisor.SetTaskbarHidden(HideToggle.IsOn);
        }
    }

    private static string Describe(TaskbarState state, bool requested) => state switch
    {
        { Hidden: true } => "The taskbar is hidden. Ctrl+Alt+Shift+H brings it back at any time.",
        { Blocked: TaskbarBlock.PanicHotkey } => "The panic hotkey restored the taskbar. Turn hiding on again when you want.",
        { Blocked: TaskbarBlock.ExplorerFighting } => "Windows kept showing the taskbar again, so Harbor stopped hiding it.",
        { Blocked: TaskbarBlock.HotkeyUnavailable } =>
            "Not hidden: another app uses Ctrl+Alt+Shift+H, and Harbor never hides the taskbar without its panic hotkey.",
        { Blocked: TaskbarBlock.TaskbarNotFound } => "Not hidden: no taskbar was found. Is Windows Explorer running?",
        { Blocked: TaskbarBlock.KillOnCloseJob } =>
            "Not hidden: the dock was started in a way that could end it without warning (a kill-on-close job), so it couldn't restore the taskbar.",
        _ when requested => "Waiting for the dock before hiding the taskbar…",
        _ => "The taskbar is visible.",
    };

    private static string Describe(TaskbarRestore restore) => restore.Result switch
    {
        null => "Explorer isn't responding. If the taskbar stays missing, restart Windows Explorer from Task Manager (Ctrl+Shift+Esc).",
        { AutoHideRestoredTo: { } state, RecordCreatedUtc: { } created } =>
            $"Taskbar restored ({restore.Reason}). Its auto-hide is {((state & 1) != 0 ? "on" : "off")} again, as before Harbor hid it on {created} (UTC).",
        { RecordKept: true, AutoHideRestoredTo: null } =>
            $"Taskbar shown ({restore.Reason}), but its auto-hide setting couldn't be restored yet. See %LOCALAPPDATA%\\Harbor\\logs.",
        { WindowsShown: > 0 } or { AutoHideRestoredTo: not null } => $"Taskbar restored ({restore.Reason}).",
        _ => "The taskbar is already visible.",
    };

    private string Describe(ControlLinkStatus status) => status switch
    {
        { Connected: false } => $"Waiting for the dock on {_host.PipeName}",
        { LastPongSeq: { } seq, RoundTrip: { } roundTrip } => $"Dock connected · pong {seq} · {roundTrip.TotalMilliseconds:0.0} ms",
        _ => "Dock connected",
    };
}
