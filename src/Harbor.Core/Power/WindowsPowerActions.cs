using System.Runtime.InteropServices;
using Harbor.Core.Logging;
using Harbor.Core.Interop;

namespace Harbor.Core.Power;

/// <summary>
/// The real power actions (docs/adr/0016). Never made for a test Shell (ShellHost.PowerActionsFor), never run by a
/// test: they lock, sleep or end the session or the PC. Sign-out and shutdown reach the dock's tested session-end
/// restore, so the taskbar is back first (rule 1).
/// </summary>
public sealed unsafe class WindowsPowerActions : IPowerActions
{
    private const uint WmSysCommand = 0x0112;
    private const nint ScMonitorPower = 0xF170;
    private const nint MonitorOff = 2;
    private const uint EwxLogOff = 0;
    private const uint ShutdownRestart = 0x4;
    private const uint ShutdownPowerOff = 0x8;
    private const uint ShutdownRestartApps = 0x80;
    // SHTDN_REASON_FLAG_PLANNED | SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER.
    private const uint ReasonPlanned = 0x80000000;
    private const uint TokenAdjustPrivileges = 0x20;
    private const uint TokenQuery = 0x8;
    private const uint PrivilegeEnabled = 0x2;

    private readonly ILog _log;
    private readonly Func<nint> _window;

    /// <param name="window">A top-level window of the Shell's, for turning the displays off (the island's).</param>
    public WindowsPowerActions(ILog log, Func<nint> window)
    {
        _log = log;
        _window = window;
        Capabilities = ReadCapabilities();
    }

    public PowerCapabilities Capabilities { get; }

    public bool Run(PowerAction action)
    {
        var done = action switch
        {
            PowerAction.Lock => SystemNative.LockWorkStation(),
            PowerAction.SignOut => SystemNative.ExitWindowsEx(EwxLogOff, ReasonPlanned),
            PowerAction.Sleep => Sleep(),
            PowerAction.Hibernate => Capabilities.CanHibernate && SystemNative.SetSuspendState(hibernate: true, force: false, disableWakeEvent: false),
            PowerAction.Restart => Shutdown(ShutdownRestart | ShutdownRestartApps),
            PowerAction.ShutDown => Shutdown(ShutdownPowerOff),
            _ => false,
        };
        var error = done ? 0 : Marshal.GetLastPInvokeError();
        _log.Info(done ? $"power: {PowerPolicy.Label(action)}" : $"power: {PowerPolicy.Label(action)} refused ({error})");
        return done;
    }

    // On a Modern Standby PC, turning the displays off enters it, as the power button does (M4 spike);
    // SetSuspendState is for PCs with S3.
    private bool Sleep()
    {
        if (Capabilities.ModernStandby)
        {
            var window = _window();
            if (window == 0)
            {
                return false;
            }
            SystemNative.SendMessage(window, WmSysCommand, ScMonitorPower, MonitorOff);
            return true;
        }
        return SystemNative.SetSuspendState(hibernate: false, force: false, disableWakeEvent: false);
    }

    private static bool Shutdown(uint flags)
    {
        if (!EnableShutdownPrivilege())
        {
            return false;
        }
        return SystemNative.InitiateShutdown(null, null, 0, flags, ReasonPlanned) == 0;
    }

    private static bool EnableShutdownPrivilege()
    {
        if (!SystemNative.OpenProcessToken(SystemNative.GetCurrentProcess(), TokenAdjustPrivileges | TokenQuery, out var token))
        {
            return false;
        }
        try
        {
            if (!SystemNative.LookupPrivilegeValue(null, "SeShutdownPrivilege", out var luid))
            {
                return false;
            }
            var privileges = new SystemNative.TokenPrivileges { Count = 1, Luid = luid, Attributes = PrivilegeEnabled };
            return SystemNative.AdjustTokenPrivileges(token, false, privileges, 0, 0, 0) && Marshal.GetLastPInvokeError() == 0;
        }
        finally
        {
            SystemNative.CloseHandle(token);
        }
    }

    // SYSTEM_POWER_CAPABILITIES: one byte each, SystemS3 at 5, SystemS4 at 6, HiberFilePresent at 8, AoAc at 20.
    private static PowerCapabilities ReadCapabilities()
    {
        var buffer = stackalloc byte[128];
        if (!SystemNative.GetPwrCapabilities(buffer))
        {
            return new PowerCapabilities(ModernStandby: false, CanHibernate: false);
        }
        return new PowerCapabilities(ModernStandby: buffer[20] != 0, CanHibernate: buffer[6] != 0 && buffer[8] != 0);
    }
}
