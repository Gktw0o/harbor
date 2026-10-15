using Harbor.Core.Logging;

namespace Harbor.Core.Apps;

/// <summary>
/// <c>&lt;data dir&gt;\icons</c>, where the Shell leaves icon files for the dock. Files are named by their content and
/// never rewritten, so the dock can't read one half-written or changed under it.
/// </summary>
public sealed class IconStore(string dataDir, ILog log)
{
    private readonly Lock _lock = new();
    private HashSet<string> _previous = [];

    public string Directory { get; } = Path.Combine(dataDir, "icons");

    /// <summary>Writes the icon unless it's there already; returns its file name, or null if it can't be written.</summary>
    public string? Save(IconImage image)
    {
        var file = IconFile.Encode(image);
        var name = IconFile.NameOf(file);
        var path = Path.Combine(Directory, name);
        if (File.Exists(path))
        {
            return name;
        }
        var temporary = Path.Combine(Directory, $"{name}.{Guid.NewGuid():N}.tmp");
        try
        {
            System.IO.Directory.CreateDirectory(Directory);
            File.WriteAllBytes(temporary, file);
            // A reader sees the whole file or none of it.
            File.Move(temporary, path, overwrite: false);
            return name;
        }
        catch (IOException) when (File.Exists(path))
        {
            // Written by someone else meanwhile: same name, same bytes.
            return name;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            log.Warn($"icons: can't write {path}: {exception.Message}");
            return null;
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    /// <summary>
    /// Deletes the icons neither this list nor the one before it uses: a dock may still draw the previous list
    /// until it gets the new one.
    /// </summary>
    public void KeepOnly(IEnumerable<string> names)
    {
        ArgumentNullException.ThrowIfNull(names);
        HashSet<string> keep;
        lock (_lock)
        {
            var current = names.ToHashSet(StringComparer.Ordinal);
            keep = [.. current, .. _previous];
            _previous = current;
        }
        if (!System.IO.Directory.Exists(Directory))
        {
            return;
        }
        foreach (var path in System.IO.Directory.EnumerateFiles(Directory))
        {
            var name = Path.GetFileName(path);
            // Temporary files are another writer's, still in progress.
            if (!keep.Contains(name) && name.EndsWith(IconFile.Extension, StringComparison.Ordinal))
            {
                try
                {
                    File.Delete(path);
                }
                catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
                {
                    log.Warn($"icons: can't delete {path}: {exception.Message}");
                }
            }
        }
    }
}
