using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace Harbor.Core.Interop;

/// <summary>IShellItem's vtable as far as Harbor uses it; methods after GetDisplayName are left out.</summary>
[GeneratedComInterface]
[Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
internal partial interface IShellItem
{
    [PreserveSig]
    int BindToHandler(nint bindContext, in Guid handler, in Guid interfaceId, out nint result);

    [PreserveSig]
    int GetParent(out nint parent);

    [PreserveSig]
    int GetDisplayName(ShellItemDisplayName kind, [MarshalAs(UnmanagedType.LPWStr)] out string? name);
}

internal enum ShellItemDisplayName
{
    /// <summary>SIGDN_NORMALDISPLAY: the name people see in Explorer.</summary>
    NormalDisplay = 0,
}

/// <summary>IShellItemImageFactory: an item's icon (or thumbnail) as a bitmap.</summary>
[GeneratedComInterface]
[Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b")]
internal partial interface IShellItemImageFactory
{
    /// <summary>bitmap: a 32-bit DIB section the caller deletes.</summary>
    [PreserveSig]
    int GetImage(ShellItems.Size size, ShellItemImageFlags flags, out nint bitmap);
}

[Flags]
internal enum ShellItemImageFlags
{
    /// <summary>SIIGBF_ICONONLY: the icon, never a thumbnail of the file's content.</summary>
    IconOnly = 0x4,
}

internal static partial class ShellItems
{
    private static readonly Guid ShellItemId = new("43826d1e-e718-42ee-bc55-a1e261c37bfe");

    /// <summary>The shell item for a parsing name (a path, or <c>shell:AppsFolder\&lt;AUMID&gt;</c>), or null if there's none.</summary>
    public static IShellItem? FromParsingName(string parsingName) =>
        SHCreateItemFromParsingName(parsingName, 0, ShellItemId, out var item) >= 0 ? item : null;

    /// <summary>Lets go of the COM object now rather than when the GC gets to it.</summary>
    public static void Release(IShellItem item) => ((ComObject)(object)item).FinalRelease();

    /// <summary>
    /// The item's icon at up to <paramref name="side"/> pixels a side, as premultiplied BGRA rows top to bottom
    /// (what the shell returns), or null.
    /// </summary>
    public static (int Width, int Height, byte[] Pixels)? IconOf(IShellItem item, int side)
    {
        var factory = (IShellItemImageFactory)(object)item;
        if (factory.GetImage(new Size { Width = side, Height = side }, ShellItemImageFlags.IconOnly, out var bitmap) < 0 || bitmap == 0)
        {
            return null;
        }
        try
        {
            return ReadBitmap(bitmap, side);
        }
        finally
        {
            DeleteObject(bitmap);
        }
    }

    private static unsafe (int Width, int Height, byte[] Pixels)? ReadBitmap(nint bitmap, int maxSide)
    {
        if (GetObjectW(bitmap, sizeof(Bitmap), out var info) == 0 || info.Width is < 1 || info.Width > maxSide ||
            Math.Abs(info.Height) is < 1 || Math.Abs(info.Height) > maxSide)
        {
            return null;
        }
        var width = info.Width;
        var height = Math.Abs(info.Height);
        var header = new BitmapInfo
        {
            Size = (uint)sizeof(BitmapInfoHeader),
            Width = width,
            // Negative: rows top to bottom.
            Height = -height,
            Planes = 1,
            BitCount = 32,
        };
        var pixels = new byte[width * height * 4];
        var screen = GetDC(0);
        try
        {
            fixed (byte* bits = pixels)
            {
                if (GetDIBits(screen, bitmap, 0, (uint)height, bits, ref header, 0) != height)
                {
                    return null;
                }
            }
        }
        finally
        {
            _ = ReleaseDC(0, screen);
        }
        return (width, height, pixels);
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Size
    {
        public int Width;
        public int Height;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Bitmap
    {
        public int Type;
        public int Width;
        public int Height;
        public int WidthBytes;
        public ushort Planes;
        public ushort BitsPixel;
        public nint Bits;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfoHeader
    {
        public uint Size;
        public int Width;
        public int Height;
        public ushort Planes;
        public ushort BitCount;
        public uint Compression;
        public uint SizeImage;
        public int XPelsPerMeter;
        public int YPelsPerMeter;
        public uint ClrUsed;
        public uint ClrImportant;
    }

    // BITMAPINFO: the header and room for one color entry, which 32-bit BI_RGB doesn't use.
    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfo
    {
        public uint Size;
        public int Width;
        public int Height;
        public ushort Planes;
        public ushort BitCount;
        public uint Compression;
        public uint SizeImage;
        public int XPelsPerMeter;
        public int YPelsPerMeter;
        public uint ClrUsed;
        public uint ClrImportant;
        public uint Colors;
    }

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int SHCreateItemFromParsingName(string path, nint bindContext, in Guid interfaceId, out IShellItem? item);

    [LibraryImport("gdi32.dll")]
    private static partial int GetObjectW(nint handle, int size, out Bitmap bitmap);

    [LibraryImport("gdi32.dll")]
    private static unsafe partial int GetDIBits(nint dc, nint bitmap, uint start, uint lines, byte* bits, ref BitmapInfo info, uint usage);

    [LibraryImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool DeleteObject(nint handle);

    [LibraryImport("user32.dll")]
    private static partial nint GetDC(nint window);

    [LibraryImport("user32.dll")]
    private static partial int ReleaseDC(nint window, nint dc);
}
