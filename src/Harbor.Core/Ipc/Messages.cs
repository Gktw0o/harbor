namespace Harbor.Core.Ipc;

/// <summary>A control-channel message. The wire format is defined by contracts/*.schema.json.</summary>
public abstract record Message;

/// <summary>Liveness probe, Harbor.Shell to dock (contracts/ping.schema.json, v1).</summary>
public sealed record Ping(uint Seq) : Message;

/// <summary>Answer to a <see cref="Ping"/>, dock to Harbor.Shell, echoing its seq (contracts/pong.schema.json, v1).</summary>
public sealed record Pong(uint Seq) : Message;

/// <summary>
/// Harbor.Shell to dock: hide the taskbar, or stop hiding it (contracts/setTaskbar.schema.json, v1). Sent on
/// every connect and whenever the user changes it.
/// </summary>
public sealed record SetTaskbar(bool Hidden) : Message;

/// <summary>
/// Dock to Harbor.Shell: whether the dock hides the taskbar now and, if it was asked to but doesn't, why
/// (contracts/taskbarState.schema.json, v1).
/// </summary>
public sealed record TaskbarState(bool Hidden, TaskbarBlock Blocked) : Message;

/// <summary>Why the dock doesn't hide the taskbar although it was asked to. Must match the dock's TaskbarBlock.</summary>
public enum TaskbarBlock
{
    None,

    /// <summary>Another app holds the panic hotkey: hiding without a way out is not allowed.</summary>
    HotkeyUnavailable,

    /// <summary>The user pressed the panic hotkey; cleared by the next <c>setTaskbar {hidden: false}</c>.</summary>
    PanicHotkey,

    /// <summary>Explorer kept showing the taskbar again; cleared like <see cref="PanicHotkey"/>.</summary>
    ExplorerFighting,

    /// <summary>There is no taskbar (e.g. Explorer isn't running).</summary>
    TaskbarNotFound,

    /// <summary>The dock runs in a job that could end it without warning, leaving nothing to restore the taskbar.</summary>
    KillOnCloseJob,
}
