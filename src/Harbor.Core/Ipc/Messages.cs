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

/// <summary>
/// Harbor.Shell to dock: the apps the dock shows, in order (contracts/dockItems.schema.json, v1). Sent on every
/// connect and whenever the list changes. Compares by content.
/// </summary>
public sealed record DockItems(IReadOnlyList<DockItem> Items) : Message
{
    // Bounds that keep the largest message under the line limit. Lengths count Unicode characters (runes).
    public const int MaxItems = 40;
    public const int MaxTargetLength = 260;
    public const int MaxNameLength = 128;

    public bool Equals(DockItems? other) => other is not null && Items.SequenceEqual(other.Items);

    public override int GetHashCode() => Items.Aggregate(0, (hash, item) => HashCode.Combine(hash, item));
}

/// <summary>
/// Dock to Harbor.Shell: a running app the dock hasn't had named yet (contracts/appLookup.schema.json, v1). The Shell
/// answers with <see cref="AppInfo"/> for the same id.
/// </summary>
/// <param name="Id">The dock's number for the app, never reused for another app. Not 0.</param>
/// <param name="Window">One of its windows (HWND), for its relaunch name and icon.</param>
public sealed record AppLookup(uint Id, string? Aumid, string? Exe, uint? Window) : Message
{
    public const int MaxAumidLength = 130;
    public const int MaxExeLength = 2048;
}

/// <summary>
/// Harbor.Shell to dock: what the Shell knows about a running app (contracts/appInfo.schema.json, v1). May be sent
/// twice for one id: at once from what the Shell knows, then with the real name and icon.
/// </summary>
/// <param name="Pinned">The pinned item this app is, if any.</param>
/// <param name="Launch">How to start it again, and what pinning it pins, if known.</param>
public sealed record AppInfo(uint Id, string Name, string? Icon, AppTarget? Pinned, AppTarget? Launch) : Message;

/// <summary>Something the dock can start or pin, as in <see cref="DockItems"/>.</summary>
public sealed record AppTarget(DockItemKind Kind, string Target);

public enum PinAction
{
    Pin,
    Unpin,
}

/// <summary>
/// Dock to Harbor.Shell: the user asked to pin or unpin an app (contracts/pinRequest.schema.json, v1). The dock
/// changes nothing until the Shell's next <see cref="DockItems"/>.
/// </summary>
public sealed record PinRequest(PinAction Action, AppTarget Item) : Message;

/// <summary>A screen rectangle in physical pixels, virtual-screen space, right and bottom exclusive.</summary>
public sealed record ScreenRect(int Left, int Top, int Right, int Bottom)
{
    public int Width => Right - Left;
    public int Height => Bottom - Top;
}

/// <summary>
/// Dock to Harbor.Shell: where the dock is and what is happening on its monitor, for the island at the top of the
/// same monitor (contracts/dockState.schema.json, v1). Sent on every connect and whenever it changes. Named apart
/// from Supervision.DockState, the dock process's state.
/// </summary>
/// <param name="Monitor">The dock's monitor.</param>
/// <param name="Work">That monitor's work area.</param>
/// <param name="Dpi">That monitor's DPI (96 = 100 %).</param>
/// <param name="FullScreen">A full-screen app is in front on that monitor (docs/adr/0014).</param>
/// <param name="Visible">The dock is shown: false after the panic hotkey, until hiding is turned on again.</param>
public sealed record DockStateMessage(ScreenRect Monitor, ScreenRect Work, int Dpi, bool FullScreen, bool Visible) : Message
{
    public const int MaxCoordinate = 65536;
    public const int MinDpi = 48;
    public const int MaxDpi = 960;
}

/// <summary>Harbor.Shell to dock: the island's state (contracts/islandState.schema.json, v1). Sent on every connect and on change.</summary>
/// <param name="Ready">The island exists and can open.</param>
/// <param name="Open">It is expanded now.</param>
/// <param name="Hotkey">The virtual-key code of its private hotkey (F13 to F24, no modifiers), which opens it; null if none.</param>
public sealed record IslandState(bool Ready, bool Open, int? Hotkey) : Message
{
    public const int MinHotkey = 0x7C;
    public const int MaxHotkey = 0x87;
}

/// <summary>
/// Harbor.Shell to dock: start an app the user chose in the island (contracts/launch.schema.json, v1), through
/// Explorer as the dock starts its own items.
/// </summary>
public sealed record Launch(AppTarget Item) : Message;

/// <summary>One app on the dock.</summary>
/// <param name="Name">Display name, for people and screen readers.</param>
/// <param name="Icon">File name of the item's icon in the icons folder (contracts/README.md), or null.</param>
public sealed record DockItem(DockItemKind Kind, string Target, string Name, string? Icon);

public enum DockItemKind
{
    /// <summary>The target is an AppUserModelID, launched as <c>shell:AppsFolder\&lt;target&gt;</c>.</summary>
    App,

    /// <summary>The target is an absolute path to an .exe or .lnk.</summary>
    Path,
}

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
