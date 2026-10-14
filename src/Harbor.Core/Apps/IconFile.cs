using System.Buffers.Binary;
using System.Security.Cryptography;

namespace Harbor.Core.Apps;

/// <summary>An icon's pixels: premultiplied BGRA, rows top to bottom, no padding.</summary>
public sealed record IconImage(int Width, int Height, byte[] Pixels);

/// <summary>
/// The dock's icon files (contracts/README.md, "Icon files"): <c>HIC1</c>, width and height, then the pixels. Raw
/// pixels so the dock needs no image decoder. Shared with the dock's core/island/icon_file.cpp.
/// </summary>
public static class IconFile
{
    public const int MaxSide = 256;
    public const string Extension = ".bgra";

    private static ReadOnlySpan<byte> Magic => "HIC1"u8;

    public static byte[] Encode(IconImage image)
    {
        ArgumentNullException.ThrowIfNull(image);
        if (image.Width is < 1 or > MaxSide || image.Height is < 1 or > MaxSide || image.Pixels.Length != image.Width * image.Height * 4)
        {
            throw new ArgumentException($"An icon is 1 to {MaxSide} pixels a side with 4 bytes per pixel.", nameof(image));
        }
        var file = new byte[12 + image.Pixels.Length];
        Magic.CopyTo(file);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(4), (uint)image.Width);
        BinaryPrimitives.WriteUInt32LittleEndian(file.AsSpan(8), (uint)image.Height);
        image.Pixels.CopyTo(file, 12);
        return file;
    }

    /// <summary>The file's name: the first 16 lowercase hex digits of its SHA-256, so a changed icon gets a new name.</summary>
    public static string NameOf(byte[] file) =>
        Convert.ToHexStringLower(SHA256.HashData(file).AsSpan(0, 8)) + Extension;

    /// <summary>
    /// Pixels as Windows gives them, made safe for the format: a bitmap without any alpha is opaque, and no color
    /// may exceed its alpha (premultiplied).
    /// </summary>
    public static void Normalize(byte[] bgra)
    {
        ArgumentNullException.ThrowIfNull(bgra);
        var anyAlpha = false;
        for (var i = 3; i < bgra.Length && !anyAlpha; i += 4)
        {
            anyAlpha = bgra[i] != 0;
        }
        for (var i = 0; i + 3 < bgra.Length; i += 4)
        {
            if (!anyAlpha)
            {
                bgra[i + 3] = 255;
            }
            var alpha = bgra[i + 3];
            bgra[i] = Math.Min(bgra[i], alpha);
            bgra[i + 1] = Math.Min(bgra[i + 1], alpha);
            bgra[i + 2] = Math.Min(bgra[i + 2], alpha);
        }
    }
}
