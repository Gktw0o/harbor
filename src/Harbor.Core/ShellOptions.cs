using Harbor.Core.Ipc;
using Harbor.Core.Taskbar;

namespace Harbor.Core;

/// <summary>
/// Harbor.Shell.exe's command line. A normal start passes nothing. Tests pass <c>--pipe-name</c>,
/// <c>--data-dir</c> and <c>--test-taskbar</c>, only ever together (as the dock does), so a test Shell never serves
/// the real pipe, never touches the real taskbar and never shares the user's files; the dock it starts gets the
/// same three. Anything else is an error.
/// </summary>
public sealed record ShellOptions
{
    /// <summary>The Shell the user runs.</summary>
    public static ShellOptions Normal { get; } = new();

    /// <summary>The control pipe to serve; null: <c>harbor.control.&lt;session&gt;</c>.</summary>
    public string? PipeName { get; init; }

    /// <summary>Logs, settings, icons and state; null: <see cref="HarborPaths.DefaultDataDir"/>.</summary>
    public string? DataDir { get; init; }

    /// <summary>The fake taskbar's id (see <see cref="TaskbarTarget.Test"/>); null: the real taskbar.</summary>
    public string? TestTaskbarId { get; init; }

    /// <summary>
    /// <c>--test-foreground</c>: the test dock may do what takes the foreground (press the island's hotkey, open
    /// Windows' Start). Tests pass it only where they may take the focus. Only in test mode.
    /// </summary>
    public bool TestForeground { get; init; }

    public bool TestMode => TestTaskbarId is not null;

    public string DataDirOrDefault => DataDir ?? HarborPaths.DefaultDataDir;

    public TaskbarTarget Taskbar => TestTaskbarId is { } id ? TaskbarTarget.Test(id, DataDirOrDefault) : TaskbarTarget.Real;

    /// <summary>What the dock the Shell starts must be told: the same pipe, data dir and test taskbar.</summary>
    public IReadOnlyList<string> DockArguments => !TestMode ? []
        : TestForeground ? ["--pipe-name", PipeName!, "--data-dir", DataDir!, "--test-taskbar", TestTaskbarId!, "--test-foreground"]
        : ["--pipe-name", PipeName!, "--data-dir", DataDir!, "--test-taskbar", TestTaskbarId!];

    /// <summary>Parses the arguments after the program name: the options, or a message for the log.</summary>
    public static (ShellOptions? Options, string? Error) Parse(IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        var options = new ShellOptions();
        for (var i = 0; i < arguments.Count; i++)
        {
            var name = arguments[i];
            if (name == "--test-foreground")
            {
                // The one option without a value.
                if (options.TestForeground)
                {
                    return (null, "--test-foreground is given more than once");
                }
                options = options with { TestForeground = true };
                continue;
            }
            if (name is not ("--pipe-name" or "--data-dir" or "--test-taskbar"))
            {
                return (null, $"unknown argument: {name}");
            }
            if (i + 1 == arguments.Count)
            {
                return (null, $"{name} needs a value");
            }
            var value = arguments[++i];
            switch (name)
            {
                case "--pipe-name" when options.PipeName is not null:
                case "--data-dir" when options.DataDir is not null:
                case "--test-taskbar" when options.TestTaskbarId is not null:
                    return (null, $"{name} is given more than once");
                case "--pipe-name" when !PipeNames.IsValid(value):
                    return (null, "--pipe-name must be \"harbor.\" followed by [a-z0-9.-]");
                case "--pipe-name":
                    options = options with { PipeName = value };
                    break;
                case "--data-dir" when !Path.IsPathFullyQualified(value):
                    return (null, "--data-dir must be an absolute path");
                case "--data-dir":
                    options = options with { DataDir = Path.GetFullPath(value) };
                    break;
                case "--test-taskbar" when !IsValidTestId(value):
                    return (null, "--test-taskbar must be 1-64 characters from [a-z0-9-]");
                default:
                    options = options with { TestTaskbarId = value };
                    break;
            }
        }
        var given = (options.PipeName is null ? 0 : 1) + (options.DataDir is null ? 0 : 1) + (options.TestTaskbarId is null ? 0 : 1);
        if (given is not (0 or 3))
        {
            return (null, "--pipe-name, --data-dir and --test-taskbar must be given together");
        }
        return options.TestForeground && given == 0
            ? (null, "--test-foreground needs --pipe-name, --data-dir and --test-taskbar")
            : (options, null);
    }

    /// <summary>The absolute <c>--data-dir</c> a command line names, even one <see cref="Parse"/> refuses; else null.</summary>
    public static string? DataDirNamedIn(IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);
        for (var i = 0; i + 1 < arguments.Count; i++)
        {
            if (arguments[i] == "--data-dir" && Path.IsPathFullyQualified(arguments[i + 1]))
            {
                return Path.GetFullPath(arguments[i + 1]);
            }
        }
        return null;
    }

    private static bool IsValidTestId(string id) =>
        id.Length is >= 1 and <= 64 && id.All(c => c is >= 'a' and <= 'z' or >= '0' and <= '9' or '-');
}
