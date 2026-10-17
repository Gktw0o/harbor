using Harbor.Core.Ipc;

namespace Harbor.Core.Island;

/// <summary>An island shape's size in DIPs and the radius of its bottom corners (the top is flush with the edge).</summary>
public readonly record struct IslandSize(double Width, double Height, double CornerRadius);

/// <summary>Where the island's window goes (docs/adr/0013): flush with the top edge of the dock's monitor, centred.</summary>
public static class IslandPlacement
{
    /// <summary>Kept clear on each side of a monitor too narrow for the panel (1024×768, say).</summary>
    public const double SideMarginDip = 16;

    /// <summary>The panel takes at most this share of the monitor's height.</summary>
    public const double MaxHeightShare = 0.8;

    public static IslandSize SizeOf(IslandShape shape) => shape switch
    {
        IslandShape.Collapsed => new(200, 30, 14),
        IslandShape.Bubble => new(320, 44, 20),
        IslandShape.Expanded => new(980, 420, 28),
        _ => new(0, 0, 0),
    };

    /// <summary>
    /// The window's rectangle in physical pixels for a shape on a monitor at a DPI: the shape's size, scaled and
    /// kept inside the monitor, centred on its top edge. Hidden: an empty rectangle there.
    /// </summary>
    public static ScreenRect WindowFor(IslandShape shape, ScreenRect monitor, int dpi)
    {
        ArgumentNullException.ThrowIfNull(monitor);
        var scale = Math.Max(dpi, 1) / 96.0;
        var size = SizeOf(shape);
        var width = (int)Math.Round(size.Width * scale);
        var height = (int)Math.Round(size.Height * scale);
        width = Math.Max(0, Math.Min(width, monitor.Width - (int)Math.Round(2 * SideMarginDip * scale)));
        height = Math.Max(0, Math.Min(height, (int)(monitor.Height * MaxHeightShare)));
        var left = monitor.Left + ((monitor.Width - width) / 2);
        return new ScreenRect(left, monitor.Top, left + width, monitor.Top + height);
    }
}
