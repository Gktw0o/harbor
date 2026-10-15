using System.Security.Cryptography;
using Harbor.Core.Logging;

namespace Harbor.Core.Settings;

/// <param name="Settings">The settings, or null if the file can't be used.</param>
/// <param name="Problem">Why not, for people; null when <paramref name="Settings"/> is set.</param>
public sealed record SettingsReadResult(HarborSettings? Settings, string? Problem);

/// <param name="Written">Whether settings.json was changed.</param>
/// <param name="Outcome">What happened, for people.</param>
public sealed record SettingsChangeResult(bool Written, string Outcome);

/// <summary>
/// <c>settings.json</c> in Harbor's data directory. Created with the defaults when it doesn't exist. An existing file
/// is only replaced by a change from the dock (pinning, docs/adr/0010), and only while it is exactly what Harbor read
/// last and decodes: a broken file, or one edited since, may hold the user's work.
/// </summary>
public sealed class SettingsFile(string dataDir, ILog log)
{
    // Far more than 40 pins can take; anything bigger isn't Harbor's.
    private const long MaxBytes = 1024 * 1024;
    private const int Attempts = 3;

    // One change at a time.
    private readonly Lock _changing = new();

    // SHA-256 of the file as Harbor last read (or wrote) it, when it decoded.
    private byte[]? _lastGood;

    public string DataDir { get; } = dataDir;

    public string FilePath { get; } = HarborSettings.PathIn(dataDir);

    /// <summary>Reads the settings, writing the defaults first if there is no file. Never throws for I/O problems.</summary>
    public async Task<SettingsReadResult> ReadAsync(CancellationToken cancellationToken = default)
    {
        for (var attempt = 1; ; attempt++)
        {
            try
            {
                return await ReadBytesAsync(cancellationToken).ConfigureAwait(false) is { } text
                    ? Decode(text)
                    : new SettingsReadResult(null, $"it is larger than {MaxBytes / 1024} KiB");
            }
            catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException)
            {
                try
                {
                    if (TryCreateWithDefaults())
                    {
                        return new SettingsReadResult(HarborSettings.Defaults, null);
                    }
                    // Someone else created it meanwhile: read theirs.
                }
                catch (Exception createException) when (createException is IOException or UnauthorizedAccessException)
                {
                    return new SettingsReadResult(null, $"it doesn't exist and can't be created ({createException.Message})");
                }
            }
            catch (IOException exception) when (attempt < Attempts)
            {
                // Most likely an editor saving it right now.
                log.Warn($"settings: can't read {FilePath} yet ({exception.Message}); trying again");
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return new SettingsReadResult(null, $"it can't be read ({exception.Message})");
            }
            if (attempt == Attempts)
            {
                return new SettingsReadResult(null, "it keeps changing while being read");
            }
            await Task.Delay(TimeSpan.FromMilliseconds(100 * attempt), cancellationToken).ConfigureAwait(false);
        }
    }

    // Null if the file is too large to be Harbor's.
    private async Task<byte[]?> ReadBytesAsync(CancellationToken cancellationToken)
    {
        // Share everything: an editor may have the file open, and a reader must never block its save.
        var stream = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete, 4096, useAsync: true);
        await using (stream.ConfigureAwait(false))
        {
            if (stream.Length > MaxBytes)
            {
                return null;
            }
            var buffer = new MemoryStream();
            await stream.CopyToAsync(buffer, cancellationToken).ConfigureAwait(false);
            return buffer.ToArray();
        }
    }

    private SettingsReadResult Decode(byte[] text)
    {
        if (!HarborSettings.TryDecode(text, out var settings, out var problem))
        {
            return new SettingsReadResult(null, problem);
        }
        Volatile.Write(ref _lastGood, SHA256.HashData(text));
        return new SettingsReadResult(settings, null);
    }

    /// <summary>
    /// Changes the settings: only if the file decodes and is exactly what Harbor read last (an edit since must not be
    /// lost), atomically (a temporary file, then a rename), one change at a time, off the caller's thread. Never
    /// throws for I/O problems.
    /// </summary>
    public Task<SettingsChangeResult> TryChangeAsync(Func<HarborSettings, PinChangeResult> change)
    {
        ArgumentNullException.ThrowIfNull(change);
        return Task.Run(() => TryChange(change));
    }

    private SettingsChangeResult TryChange(Func<HarborSettings, PinChangeResult> change)
    {
        lock (_changing)
        {
            byte[]? text;
            try
            {
                text = ReadBytes();
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return new SettingsChangeResult(false, $"settings.json can't be read ({exception.Message})");
            }
            if (text is null || !HarborSettings.TryDecode(text, out var settings, out _))
            {
                return new SettingsChangeResult(false, "settings.json has a problem; fix it first");
            }
            if (Volatile.Read(ref _lastGood) is not { } lastGood || !SHA256.HashData(text).AsSpan().SequenceEqual(lastGood))
            {
                return new SettingsChangeResult(false, "settings.json changed since Harbor read it; try again in a moment");
            }
            var result = change(settings);
            if (result.Settings is null)
            {
                return new SettingsChangeResult(false, result.Outcome);
            }
            var updated = HarborSettings.Encode(result.Settings);
            var temporary = FilePath + $".{Guid.NewGuid():N}.tmp";
            try
            {
                File.WriteAllBytes(temporary, updated);
                File.Move(temporary, FilePath, overwrite: true);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                return new SettingsChangeResult(false, $"settings.json can't be written ({exception.Message})");
            }
            finally
            {
                File.Delete(temporary);
            }
            // Harbor's own change: the next one needn't wait for the watcher to read it back.
            Volatile.Write(ref _lastGood, SHA256.HashData(updated));
            return new SettingsChangeResult(true, result.Outcome);
        }
    }

    // Null if the file is too large to be Harbor's. Shares everything, like ReadBytesAsync.
    private byte[]? ReadBytes()
    {
        using var stream = new FileStream(FilePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        if (stream.Length > MaxBytes)
        {
            return null;
        }
        var buffer = new MemoryStream();
        stream.CopyTo(buffer);
        return buffer.ToArray();
    }

    // Writes a temporary file and moves it into place without replacing anything, so a reader never sees half a
    // file and nobody's file is overwritten. False if a settings file appeared first.
    private bool TryCreateWithDefaults()
    {
        Directory.CreateDirectory(DataDir);
        var temporary = FilePath + $".{Guid.NewGuid():N}.tmp";
        try
        {
            var defaults = HarborSettings.Encode(HarborSettings.Defaults);
            File.WriteAllBytes(temporary, defaults);
            File.Move(temporary, FilePath, overwrite: false);
            Volatile.Write(ref _lastGood, SHA256.HashData(defaults));
            log.Info($"settings: created {FilePath} with the default pinned apps");
            return true;
        }
        catch (IOException) when (File.Exists(FilePath))
        {
            return false;
        }
        finally
        {
            File.Delete(temporary);
        }
    }
}
