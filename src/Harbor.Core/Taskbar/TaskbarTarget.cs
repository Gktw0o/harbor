namespace Harbor.Core.Taskbar;

/// <summary>
/// Which windows Harbor treats as the taskbar. Must match the dock's adapters/win32_taskbar.h.
/// <list type="bullet">
/// <item><see cref="Real"/>: Explorer's <c>Shell_TrayWnd</c> and <c>Shell_SecondaryTrayWnd</c> (owned by the
/// shell process) and the real auto-hide setting.</item>
/// <item><see cref="Test"/>: windows of the classes <c>Harbor.Test.&lt;id&gt;.TrayWnd</c> and
/// <c>.SecondaryTrayWnd</c> that a test creates, and an auto-hide "setting" kept in
/// <c>&lt;dataDir&gt;\test-autohide-state.txt</c>. Tests never touch the user's taskbar, and never create
/// windows with Explorer's class names (that has broken Explorer for other projects).</item>
/// </list>
/// </summary>
public sealed class TaskbarTarget
{
    private TaskbarTarget(bool isReal, string primaryClass, string secondaryClass, string? testAutoHideFile)
    {
        IsReal = isReal;
        PrimaryClass = primaryClass;
        SecondaryClass = secondaryClass;
        TestAutoHideFile = testAutoHideFile;
    }

    public static TaskbarTarget Real { get; } = new(isReal: true, "Shell_TrayWnd", "Shell_SecondaryTrayWnd", null);

    public bool IsReal { get; }

    public string PrimaryClass { get; }

    public string SecondaryClass { get; }

    public string? TestAutoHideFile { get; }

    public static TaskbarTarget Test(string id, string dataDir) => new(
        isReal: false,
        $"Harbor.Test.{id}.TrayWnd",
        $"Harbor.Test.{id}.SecondaryTrayWnd",
        Path.Combine(dataDir, "test-autohide-state.txt"));
}
