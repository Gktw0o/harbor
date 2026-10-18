using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using Harbor.Core.Interop;
using Harbor.Core.Logging;

namespace Harbor.Core.Apps;

/// <summary>Where the island's list of installed apps comes from.</summary>
public interface IAppSource
{
    /// <summary>Every app, in no particular order; may take a second (call it off the UI thread).</summary>
    IReadOnlyList<AppEntry> ReadAll();
}

/// <summary>The apps Start lists: shell:AppsFolder (docs/adr/0016). Names and ids only, about 25 KB.</summary>
public sealed class AppsFolderSource : IAppSource
{
    public IReadOnlyList<AppEntry> ReadAll() => [.. ShellItems.AppsFolder().Select(app => new AppEntry(app.Name, app.Id))];
}

/// <summary>
/// Test Shells only: the apps listed in a JSON file (<c>[{"name": ..., "id": ...}]</c>), so tests find their own
/// marker app and never depend on what the machine has installed.
/// </summary>
public sealed class FileAppSource(string path) : IAppSource
{
    public const string FileName = "test-apps.json";

    public IReadOnlyList<AppEntry> ReadAll()
    {
        if (!File.Exists(path))
        {
            return [];
        }
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        return [.. document.RootElement.EnumerateArray()
            .Select(app => new AppEntry(app.GetProperty("name").GetString() ?? "", app.GetProperty("id").GetString() ?? ""))
            .Where(app => app.Name.Length > 0 && app.Id.Length > 0)];
    }
}

/// <summary>
/// The island's searchable list of apps (docs/adr/0016): read off the UI thread when the Shell starts, again when
/// the island opens after a while (apps come and go), never more than one read at a time.
/// </summary>
public sealed class AppIndex(IAppSource source, ILog log)
{
    /// <summary>A list older than this is read again when the island opens.</summary>
    public static readonly TimeSpan MaxAge = TimeSpan.FromMinutes(1);

    private readonly Lock _lock = new();
    private Task? _reading;
    private DateTime _readAt = DateTime.MinValue;

    /// <summary>The latest list; empty until the first read ends.</summary>
    public AppSearch Search { get; private set; } = new([]);

    /// <summary>A new list arrived. Raised on a background thread.</summary>
    public event Action? Changed;

    /// <summary>Reads the apps again in the background unless a read is under way or the list is fresh (or forced).</summary>
    public Task RefreshAsync(bool force = false)
    {
        lock (_lock)
        {
            if (_reading is { IsCompleted: false })
            {
                return _reading;
            }
            if (!force && DateTime.UtcNow - _readAt < MaxAge)
            {
                return Task.CompletedTask;
            }
            _readAt = DateTime.UtcNow;
            _reading = Task.Run(Read);
            return _reading;
        }
    }

    private void Read()
    {
        var watch = Stopwatch.StartNew();
        try
        {
            var apps = source.ReadAll();
            Search = new AppSearch(apps);
            // How many and how long; never which.
            log.Info($"apps: {apps.Count} in the island's list ({watch.ElapsedMilliseconds} ms)");
            Changed?.Invoke();
        }
        catch (Exception exception) when (exception is COMException or JsonException or IOException or InvalidOperationException or KeyNotFoundException)
        {
            log.Warn($"apps: couldn't read the list: {exception.Message}");
        }
    }
}
