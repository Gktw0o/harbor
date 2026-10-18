using Harbor.Core.Logging;

namespace Harbor.Core.Power;

/// <summary>What the island's Power section can do (docs/adr/0016).</summary>
public enum PowerAction
{
    Lock,
    SignOut,
    Sleep,
    Hibernate,
    Restart,
    ShutDown,
}

/// <summary>What this PC can do, as Windows reports it (GetPwrCapabilities).</summary>
/// <param name="ModernStandby">Sleep is modern standby (no S3): turning the displays off enters it, as the power button does.</param>
/// <param name="CanHibernate">Hibernate is enabled.</param>
public sealed record PowerCapabilities(bool ModernStandby, bool CanHibernate);

/// <summary>Port: the power actions. Destructive by nature: test Shells get <see cref="RecordingPowerActions"/>.</summary>
public interface IPowerActions
{
    PowerCapabilities Capabilities { get; }

    /// <summary>Does it; false if Windows refused (the reason is logged).</summary>
    bool Run(PowerAction action);
}

/// <summary>The rules around the actions, pure.</summary>
public static class PowerPolicy
{
    /// <summary>Ending the session or the PC asks first (docs/adr/0016); locking and sleeping don't.</summary>
    public static bool NeedsConfirmation(PowerAction action) => action is PowerAction.SignOut or PowerAction.Restart or PowerAction.ShutDown;

    /// <summary>The actions to offer, in order: hibernate only where it is enabled.</summary>
    public static IReadOnlyList<PowerAction> Offered(PowerCapabilities capabilities)
    {
        ArgumentNullException.ThrowIfNull(capabilities);
        return capabilities.CanHibernate
            ? [PowerAction.Lock, PowerAction.SignOut, PowerAction.Sleep, PowerAction.Hibernate, PowerAction.Restart, PowerAction.ShutDown]
            : [PowerAction.Lock, PowerAction.SignOut, PowerAction.Sleep, PowerAction.Restart, PowerAction.ShutDown];
    }

    public static string Label(PowerAction action) => action switch
    {
        PowerAction.Lock => "Lock",
        PowerAction.SignOut => "Sign out",
        PowerAction.Sleep => "Sleep",
        PowerAction.Hibernate => "Hibernate",
        PowerAction.Restart => "Restart",
        _ => "Shut down",
    };
}

/// <summary>
/// Test Shells' power actions: nothing happens, what was asked is recorded and logged. A test can never lock,
/// sleep or shut down the machine it runs on.
/// </summary>
public sealed class RecordingPowerActions(ILog log, PowerCapabilities? capabilities = null) : IPowerActions
{
    private readonly List<PowerAction> _asked = [];

    public PowerCapabilities Capabilities { get; } = capabilities ?? new PowerCapabilities(ModernStandby: true, CanHibernate: true);

    public IReadOnlyList<PowerAction> Asked
    {
        get
        {
            lock (_asked)
            {
                return [.. _asked];
            }
        }
    }

    public bool Run(PowerAction action)
    {
        lock (_asked)
        {
            _asked.Add(action);
        }
        log.Info($"power: {PowerPolicy.Label(action)} asked (a test Shell: nothing done)");
        return true;
    }
}
