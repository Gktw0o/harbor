namespace Harbor.Core;

/// <summary>Where Harbor keeps its files. Must match the dock's adapters/data_dir.cpp.</summary>
public static class HarborPaths
{
    /// <summary><c>%LOCALAPPDATA%\Harbor</c>: logs and state.</summary>
    public static string DefaultDataDir { get; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Harbor");

    public static string LogsDir(string dataDir) => Path.Combine(dataDir, "logs");
}
