using System.ComponentModel;
using System.Runtime.InteropServices;
using Harbor.Core.Interop;
using Harbor.Core.Logging;
using Microsoft.Win32.SafeHandles;

namespace Harbor.Core.Supervision;

/// <summary>
/// A dock process the watchdog holds a handle to. The handle keeps the process id from being reused, and
/// lets the watchdog wait for the process to end and end it.
/// </summary>
internal sealed class DockProcess : IDisposable
{
    private readonly SafeProcessHandle _handle;
    private readonly ProcessWaitHandle _exitSignal;
    private readonly RegisteredWaitHandle _registration;
    private readonly TaskCompletionSource<int> _exited = new(TaskCreationOptions.RunContinuationsAsynchronously);

    private DockProcess(SafeProcessHandle handle, uint id)
    {
        _handle = handle;
        Id = id;
        _exitSignal = new ProcessWaitHandle(handle);
        _registration = ThreadPool.RegisterWaitForSingleObject(
            _exitSignal, (_, _) => _exited.TrySetResult(ReadExitCode()), null, Timeout.Infinite, executeOnlyOnce: true);
    }

    public uint Id { get; }

    /// <summary>Completes with the exit code once the process has ended.</summary>
    public Task<int> Exited => _exited.Task;

    public bool IsDebuggerAttached => NativeMethods.CheckRemoteDebuggerPresent(_handle, out var present) && present;

    /// <summary>
    /// Starts the dock outside any job Harbor Shell runs in (a terminal's or an IDE's) when that job allows it:
    /// a job that ends both processes at once would leave nothing to restore the taskbar.
    /// </summary>
    public static DockProcess Launch(string exePath, IReadOnlyList<string> arguments, ILog log)
    {
        var commandLine = CommandLine.Join([exePath, .. arguments]);
        if (!TryCreate(exePath, commandLine, NativeMethods.CreateBreakawayFromJob, out var info, out var error))
        {
            if (error != NativeMethods.ErrorAccessDenied)
            {
                throw new Win32Exception(error);
            }
            // The job doesn't allow breaking away. The dock checks its job and refuses to hide the taskbar if the
            // job could end it without warning.
            if (!TryCreate(exePath, commandLine, 0, out info, out error))
            {
                throw new Win32Exception(error);
            }
            log.Warn("watchdog: started the dock inside Harbor Shell's job (it doesn't allow breaking away)");
        }
        NativeMethods.CloseHandle(info.Thread);
        return new DockProcess(new SafeProcessHandle(info.Process, ownsHandle: true), info.ProcessId);
    }

    private static bool TryCreate(
        string exePath, string commandLine, uint flags, out NativeMethods.ProcessInformation info, out int error)
    {
        // CreateProcessW may write to the command line, so each attempt gets its own copy.
        var buffer = Marshal.StringToHGlobalUni(commandLine);
        try
        {
            var startup = new NativeMethods.StartupInfo { Size = (uint)Marshal.SizeOf<NativeMethods.StartupInfo>() };
            var created = NativeMethods.CreateProcess(
                exePath, buffer, 0, 0, inheritHandles: false, flags, 0, null, in startup, out info);
            error = created ? 0 : Marshal.GetLastPInvokeError();
            return created;
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    /// <summary>
    /// Supervises a dock that was started some other way. Null if this process may not wait for it and end it
    /// (e.g. it runs elevated): such a dock must not be asked to hide the taskbar.
    /// </summary>
    public static DockProcess? Adopt(uint processId, ILog log)
    {
        var handle = NativeMethods.OpenProcess(
            NativeMethods.Synchronize | NativeMethods.ProcessTerminate | NativeMethods.ProcessQueryInformation, false, processId);
        if (handle.IsInvalid)
        {
            log.Warn($"watchdog: can't supervise dock process {processId} (error {Marshal.GetLastPInvokeError()})");
            handle.Dispose();
            return null;
        }
        return new DockProcess(handle, processId);
    }

    public void Kill() => NativeMethods.TerminateProcess(_handle, DockExitCodes.KilledByWatchdog);

    public void Dispose()
    {
        _registration.Unregister(null);
        _exitSignal.Dispose();
        _handle.Dispose();
    }

    private int ReadExitCode() => NativeMethods.GetExitCodeProcess(_handle, out var code) ? unchecked((int)code) : -1;

    // Waits on the process handle itself; _handle keeps owning it.
    private sealed class ProcessWaitHandle : WaitHandle
    {
        public ProcessWaitHandle(SafeProcessHandle process) =>
            SafeWaitHandle = new SafeWaitHandle(process.DangerousGetHandle(), ownsHandle: false);
    }
}
