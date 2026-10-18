using Harbor.Core.Ipc;

namespace Harbor.Core.Island;

/// <summary>What the island looks like (docs/adr/0013).</summary>
public enum IslandShape
{
    /// <summary>Not shown: a full-screen app is in front on the dock's monitor, or the panic hotkey took the dock away.</summary>
    Hidden,

    /// <summary>The empty glass notch on the top edge.</summary>
    Collapsed,

    /// <summary>Grown a little while the mouse is on it.</summary>
    Bubble,

    /// <summary>Open: the panel with its sections; it has the keyboard focus.</summary>
    Expanded,
}

/// <summary>
/// The island as a pure state machine (docs/adr/0013). It follows the dock (dockState: where it is, a full-screen
/// app there, the panic hotkey), the mouse on the notch, clicks, the island's hotkey (the Start button and, later,
/// the Windows key) and dismissals (Esc, losing the focus, an app started from it).
/// </summary>
public sealed class IslandModel
{
    private bool _pointerOver;

    public IslandShape Shape { get; private set; } = IslandShape.Collapsed;

    /// <summary>Expanded: what <c>islandState.open</c> reports.</summary>
    public bool Open => Shape == IslandShape.Expanded;

    /// <summary>The dock's last dockState; null while no dock is connected (the island then sits on the primary monitor).</summary>
    public DockStateMessage? Dock { get; private set; }

    /// <summary>The panic hotkey took the dock away: nothing of Harbor's may stay up, the island included.</summary>
    private bool Panicked => Dock is { Visible: false };

    /// <summary>Where the island rests when nothing else holds it.</summary>
    private IslandShape Resting => Panicked || Dock is { FullScreen: true } ? IslandShape.Hidden
        : _pointerOver ? IslandShape.Bubble
        : IslandShape.Collapsed;

    /// <summary>A dockState came; null: the dock is gone.</summary>
    public void OnDockState(DockStateMessage? state)
    {
        Dock = state;
        if (Shape == IslandShape.Expanded && !Panicked)
        {
            // Opened on purpose: it stays open over a full-screen app, and follows the dock to another monitor.
            return;
        }
        if (Panicked)
        {
            _pointerOver = false;
        }
        Shape = Resting;
    }

    /// <summary>The mouse is on the notch or the bubble (true), or left it (false).</summary>
    public void OnPointer(bool over)
    {
        _pointerOver = over && Shape != IslandShape.Hidden;
        if (Shape is IslandShape.Collapsed or IslandShape.Bubble)
        {
            Shape = Resting;
        }
    }

    /// <summary>A click on the notch or the bubble opens the island.</summary>
    public void OnClick()
    {
        if (Shape is IslandShape.Collapsed or IslandShape.Bubble)
        {
            Shape = IslandShape.Expanded;
        }
    }

    /// <summary>The island's hotkey: opens it, even over a full-screen app; closes it if it is open. Not after the panic hotkey.</summary>
    public void OnHotkey()
    {
        if (Panicked)
        {
            return;
        }
        if (Shape == IslandShape.Expanded)
        {
            OnDismiss();
            return;
        }
        Shape = IslandShape.Expanded;
    }

    /// <summary>Esc, the focus going elsewhere, or an app started from the island: it closes.</summary>
    public void OnDismiss()
    {
        if (Shape != IslandShape.Expanded)
        {
            return;
        }
        // The mouse may still be on it, but it closes to the notch; it grows again when the mouse moves.
        _pointerOver = false;
        Shape = Resting;
    }
}
