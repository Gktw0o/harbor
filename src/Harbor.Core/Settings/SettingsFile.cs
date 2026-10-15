using Harbor.Core.Logging;

namespace Harbor.Core.Settings;

/// <param name="Settings">The settings, or null if the file can't be used.</param>
/// <param name="Problem">Why not, for people; null when <paramref name="Settings"/> is set.</param>
public sealed record SettingsReadResult(HarborSettings? Settings, string? Problem);

/// <summary>
/// <c>settings.json</c> in Harbor's data directory. Created with the defaults when it doesn't exist; an existing
/// file is never overwritten, even a broken one: it may hold the user's edits.
/// </summary>
public sealed class SettingsFile(string dataDir, ILog log)
{
    // Far more than 40 pins can take; anything bigger isn't Harbor's.
    private const long MaxBytes = 1024 * 1024;
    private const int Attempts = 3;

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

    private static SettingsReadResult Decode(byte[] text) =>
        HarborSettings.TryDecode(text, out var settings, out var problem)
            ? new SettingsReadResult(settings, null)
            : new SettingsReadResult(null, problem);

    // Writes a temporary file and moves it into place without replacing anything, so a reader never sees half a
    // file and nobody's file is overwritten. False if a settings file appeared first.
    private bool TryCreateWithDefaults()
    {
        Directory.CreateDirectory(DataDir);
        var temporary = FilePath + $".{Guid.NewGuid():N}.tmp";
        try
        {
            File.WriteAllBytes(temporary, HarborSettings.Encode(HarborSettings.Defaults));
            File.Move(temporary, FilePath, overwrite: false);
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
