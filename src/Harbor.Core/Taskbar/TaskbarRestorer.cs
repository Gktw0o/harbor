using System.Globalization;
using Harbor.Core.Logging;
using static Harbor.Core.Interop.NativeMethods;

namespace Harbor.Core.Taskbar;

public readonly record struct TaskbarWindow(nint Handle, bool IsPrimary);

public sealed record RestoreOptions
{
    /// <summary>
    /// The session is ending: restore, but keep the record, because Explorer may save its settings before
    /// ours land. The next start applies it again.
    /// </summary>
    public bool SessionEnding { get; init; }

    /// <summary><c>ABM_SETSTATE</c> has been seen to revert itself, so the state is checked again after this long.</summary>
    public TimeSpan VerifyDelay { get; init; } = TimeSpan.FromSeconds(1);
}

/// <param name="WindowsShown">Hidden taskbar windows that were shown again.</param>
/// <param name="AutoHideRestoredTo">The auto-hide state put back from the record, if there was one and it held.</param>
/// <param name="RecordKept">A record is still on disk: unverified, unreadable, or the session is ending.</param>
/// <param name="RecordCreatedUtc">When the applied record was written, if it was readable.</param>
public sealed record RestoreResult(int WindowsShown, uint? AutoHideRestoredTo, bool RecordKept, string? RecordCreatedUtc);

/// <summary>
/// The Shell's side of restoring the taskbar, with the same behavior as the dock's adapters/win32_taskbar.cpp
/// (both run the same test cases). Restoring needs no state from whoever hid the taskbar.
/// <para>Only the dock touches auto-hide while it is alive. So while a dock runs, the Shell only calls
/// <see cref="ShowHiddenWindows"/>; it calls <see cref="Restore"/> once no dock is running.</para>
/// </summary>
public sealed class TaskbarRestorer(TaskbarTarget target, string? recordPath, ILog log)
{
    public static bool IsSessionEnding => GetSystemMetrics(SmShuttingDown) != 0;

    /// <summary>The taskbar's windows, primary first. Empty if there is no taskbar (e.g. Explorer isn't running).</summary>
    public IReadOnlyList<TaskbarWindow> FindWindows()
    {
        // Only Explorer's own taskbar: other apps (e.g. tray widgets) create windows with the same classes.
        uint shellProcess = 0;
        if (target.IsReal)
        {
            var shell = GetShellWindow();
            if (shell == 0 || GetWindowThreadProcessId(shell, out shellProcess) == 0)
            {
                return [];
            }
        }

        var windows = new List<TaskbarWindow>();
        foreach (var (windowClass, isPrimary) in new[] { (target.PrimaryClass, true), (target.SecondaryClass, false) })
        {
            nint window = 0;
            while ((window = FindWindowEx(0, window, windowClass, null)) != 0)
            {
                GetWindowThreadProcessId(window, out var owner);
                if (!target.IsReal || owner == shellProcess)
                {
                    windows.Add(new TaskbarWindow(window, isPrimary));
                }
            }
        }
        return windows;
    }

    /// <summary><c>ABM_GETSTATE</c> bits (<c>ABS_AUTOHIDE</c> = 1), or the test file's value (0 if it doesn't exist).</summary>
    public uint GetAutoHideState()
    {
        if (target.IsReal)
        {
            var data = new AppBarData { Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<AppBarData>() };
            return (uint)SHAppBarMessage(AbmGetState, ref data);
        }
        try
        {
            // Shared, so reading it never makes the dock's write fail.
            using var reader = new StreamReader(new FileStream(
                target.TestAutoHideFile!, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete));
            return uint.TryParse(reader.ReadToEnd(), NumberStyles.None, CultureInfo.InvariantCulture, out var state) ? state : 0;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return 0;
        }
    }

    public bool SetAutoHideState(uint state)
    {
        if (target.IsReal)
        {
            var windows = FindWindows();
            var data = new AppBarData
            {
                Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<AppBarData>(),
                Window = windows.Count > 0 ? windows[0].Handle : 0,
                Param = (nint)state,
            };
            // Always returns TRUE, and the change has been seen to revert itself: callers verify with GetAutoHideState.
            SHAppBarMessage(AbmSetState, ref data);
            return true;
        }
        try
        {
            File.WriteAllText(target.TestAutoHideFile!, state.ToString(CultureInfo.InvariantCulture));
            return true;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    /// <summary>
    /// Shows the primary taskbar and every secondary one on a connected monitor, and returns how many were
    /// hidden. Never blocks (<c>ShowWindowAsync</c>), so it is safe while Explorer or the dock is unresponsive.
    /// </summary>
    public int ShowHiddenWindows()
    {
        var shown = 0;
        foreach (var window in FindWindows())
        {
            // A secondary taskbar whose monitor is gone stays as Explorer left it.
            if (!window.IsPrimary && MonitorFromWindow(window.Handle, MonitorDefaultToNull) == 0)
            {
                continue;
            }
            // Sent even to windows that look visible: a hide may still be queued for one (ShowWindowAsync),
            // and this show must land after it. Showing a visible window changes nothing.
            var wasHidden = !IsWindowVisible(window.Handle);
            if (ShowWindowAsync(window.Handle, SwShowNa) && wasHidden)
            {
                shown++;
            }
        }
        return shown;
    }

    /// <summary>
    /// After a dock ended: tells Explorer to drop the screen strip (AppBar) its island reserved. Explorer frees a
    /// dead window's strip by itself; this is the belt to those braces. Only for the real taskbar: tests never
    /// touch the user's work area. Can block on Explorer.
    /// </summary>
    public bool ReleaseAppBar(nint island)
    {
        if (!target.IsReal || island == 0)
        {
            return false;
        }
        var data = new AppBarData { Size = (uint)System.Runtime.InteropServices.Marshal.SizeOf<AppBarData>(), Window = island };
        SHAppBarMessage(AbmRemove, ref data);
        return true;
    }

    /// <summary>
    /// <see cref="ShowHiddenWindows"/>, then: if a restore record exists, puts auto-hide back as recorded (off if
    /// the record is damaged) and deletes the record once that is verified. Can block on Explorer.
    /// </summary>
    public RestoreResult Restore(RestoreOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        var shown = ShowHiddenWindows();
        if (shown > 0)
        {
            log.Info($"taskbar: showed {shown} hidden taskbar window(s)");
        }
        if (recordPath is null || !File.Exists(recordPath))
        {
            return new RestoreResult(shown, null, RecordKept: false, null);
        }

        byte[] text;
        try
        {
            text = File.ReadAllBytes(recordPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            log.Error("taskbar: can't read the restore record; keeping it to try again");
            return new RestoreResult(shown, null, RecordKept: true, null);
        }

        RestoreRecord.TryDecode(text, out var record);
        // Records are only written when auto-hide was off, so off is the right answer for a damaged one.
        var state = record?.StateBefore ?? 0;
        var recorded = record is null ? " (damaged record)" : $" (recorded {record.CreatedUtc})";

        SetAutoHideState(state);
        var held = AutoHideIs(state);
        if (held && options.VerifyDelay > TimeSpan.Zero)
        {
            Thread.Sleep(options.VerifyDelay);
            held = AutoHideIs(state);
        }
        if (!held)
        {
            log.Error($"taskbar: auto-hide didn't go back to {Name(state)}{recorded}; keeping the restore record to try again");
            return new RestoreResult(shown, null, RecordKept: true, record?.CreatedUtc);
        }

        if (options.SessionEnding)
        {
            log.Info($"taskbar: auto-hide restored to {Name(state)}{recorded}; the session is ending, so the record stays until the next start");
            return new RestoreResult(shown, state, RecordKept: true, record?.CreatedUtc);
        }
        try
        {
            File.Delete(recordPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            log.Error($"taskbar: auto-hide restored to {Name(state)}{recorded}, but the record can't be deleted ({exception.Message})");
            return new RestoreResult(shown, state, RecordKept: true, record?.CreatedUtc);
        }
        log.Info($"taskbar: auto-hide restored to {Name(state)}{recorded}");
        return new RestoreResult(shown, state, RecordKept: false, record?.CreatedUtc);
    }

    /// <summary>
    /// <see cref="Restore"/> on the thread pool, giving up after <paramref name="timeout"/> (Explorer may be
    /// hung). Returns null on timeout; the restore then finishes in the background if it ever can.
    /// </summary>
    public async Task<RestoreResult?> RestoreAsync(RestoreOptions options, TimeSpan timeout)
    {
        try
        {
            return await Task.Run(() => Restore(options)).WaitAsync(timeout).ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            log.Error("taskbar: Explorer didn't respond in time; the restore continues in the background");
            return null;
        }
    }

    // Since Windows 7, ABM_GETSTATE never reports ABS_ALWAYSONTOP; only the auto-hide bit is comparable.
    private bool AutoHideIs(uint state) => (GetAutoHideState() & AbsAutoHide) == (state & AbsAutoHide);

    private static string Name(uint state) => (state & AbsAutoHide) != 0 ? "on" : "off";
}
