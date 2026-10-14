namespace Harbor.Core.Tests;

// A fresh directory under %TEMP%, deleted with everything in it on dispose.
internal sealed class TempDirectory : IDisposable
{
    public TempDirectory(string suffix = "")
    {
        Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"harbor-test-{Guid.NewGuid():N}{suffix}");
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    public void Dispose()
    {
        try
        {
            Directory.Delete(Path, recursive: true);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
        }
    }
}
