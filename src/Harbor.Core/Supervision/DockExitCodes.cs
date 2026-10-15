namespace Harbor.Core.Supervision;

/// <summary>harbor-dock.exe's own exit codes. Must match src/dock/app/exit_codes.h.</summary>
public static class DockExitCodes
{
    /// <summary>The command line was rejected.</summary>
    public const int InvalidArguments = 87;

    /// <summary>Another dock already runs for this session (or this test id).</summary>
    public const int AlreadyRunning = 0x20000001;

    /// <summary>Used by the watchdog when it ends a hung dock.</summary>
    public const int KilledByWatchdog = 0x20000002;

    /// <summary>The dock can't draw (composition failed to start): starting it again won't help.</summary>
    public const int GraphicsUnavailable = 0x20000003;
}
