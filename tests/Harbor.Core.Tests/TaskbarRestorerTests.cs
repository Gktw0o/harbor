using Harbor.Core.Taskbar;

namespace Harbor.Core.Tests;

// The restore path against stand-in taskbar windows (TaskbarTarget.Test). Same cases as
// tests/dock/win32_taskbar_tests.cpp, which tests the dock's implementation.
[TestClass]
public sealed class TaskbarRestorerTests : IDisposable
{
    private static readonly RestoreOptions NoDelay = new() { VerifyDelay = TimeSpan.Zero };

    private readonly TempDirectory _dir = new();
    private readonly CapturingLog _log = new();
    private readonly FakeTaskbar _fake;
    private readonly TaskbarRestorer _restorer;

    public TaskbarRestorerTests()
    {
        _fake = new FakeTaskbar(_dir.Path, secondaries: 1, offscreenSecondaries: 1);
        _restorer = new TaskbarRestorer(_fake.Target, RecordPath, _log);
    }

    private string RecordPath => RestoreRecord.PathIn(_dir.Path);

    public void Dispose()
    {
        _fake.Dispose();
        _dir.Dispose();
    }

    private void WriteRecord(uint stateBefore) =>
        File.WriteAllBytes(RecordPath, RestoreRecord.Encode(new RestoreRecord(stateBefore, "2026-10-14T15:30:00Z")));

    [TestMethod]
    public void FindsTheTaskbarWindowsPrimaryFirst()
    {
        var windows = _restorer.FindWindows();
        Assert.HasCount(3, windows);
        Assert.AreEqual(new TaskbarWindow(_fake.Primary, IsPrimary: true), windows[0]);
        Assert.IsFalse(windows[1].IsPrimary);
        Assert.IsFalse(windows[2].IsPrimary);
    }

    [TestMethod]
    public void RestoreShowsHiddenTaskbarWindows()
    {
        FakeTaskbar.Hide(_fake.Primary);
        FakeTaskbar.Hide(_fake.Secondaries[0]);

        Assert.AreEqual(2, _restorer.Restore(NoDelay).WindowsShown);
        Assert.IsTrue(FakeTaskbar.WaitUntilVisible(_fake.Primary, visible: true));
        Assert.IsTrue(FakeTaskbar.WaitUntilVisible(_fake.Secondaries[0], visible: true));
    }

    [TestMethod]
    public void LeavesASecondaryTaskbarOnAMissingMonitorAlone()
    {
        FakeTaskbar.Hide(_fake.OffscreenSecondaries[0]);

        Assert.AreEqual(0, _restorer.Restore(NoDelay).WindowsShown);
        Assert.IsFalse(FakeTaskbar.IsVisible(_fake.OffscreenSecondaries[0]));
    }

    [TestMethod]
    public void LeavesOtherWindowsAlone()
    {
        using var other = new FakeTaskbar(_dir.Path);
        FakeTaskbar.Hide(other.Primary);

        Assert.AreEqual(0, _restorer.Restore(NoDelay).WindowsShown);
        Assert.IsFalse(FakeTaskbar.IsVisible(other.Primary));
    }

    [TestMethod]
    public void WithoutARecordAutoHideIsLeftAlone()
    {
        _fake.AutoHide = 1;

        var result = _restorer.Restore(NoDelay);
        Assert.AreEqual(1u, _fake.AutoHide);
        Assert.IsNull(result.AutoHideRestoredTo);
        Assert.IsFalse(result.RecordKept);
    }

    [TestMethod]
    public void AppliesTheRecordThenDeletesIt()
    {
        _fake.AutoHide = 1;
        WriteRecord(0);

        var result = _restorer.Restore(NoDelay);
        Assert.AreEqual(0u, _fake.AutoHide);
        Assert.AreEqual(0u, result.AutoHideRestoredTo);
        Assert.AreEqual("2026-10-14T15:30:00Z", result.RecordCreatedUtc);
        Assert.IsFalse(result.RecordKept);
        Assert.IsFalse(File.Exists(RecordPath));
        Assert.IsTrue(_log.Contains("auto-hide restored to off (recorded 2026-10-14T15:30:00Z)"));
    }

    [TestMethod]
    public void ADamagedRecordRestoresAutoHideOff()
    {
        _fake.AutoHide = 1;
        File.WriteAllText(RecordPath, "{\"v\":1,\"stateBefore\":");

        var result = _restorer.Restore(NoDelay);
        Assert.AreEqual(0u, _fake.AutoHide);
        Assert.AreEqual(0u, result.AutoHideRestoredTo);
        Assert.IsFalse(File.Exists(RecordPath));
    }

    [TestMethod]
    public void KeepsTheRecordWhileTheSessionEnds()
    {
        _fake.AutoHide = 1;
        WriteRecord(0);

        var result = _restorer.Restore(NoDelay with { SessionEnding = true });
        Assert.AreEqual(0u, _fake.AutoHide);
        Assert.IsTrue(result.RecordKept);
        Assert.IsTrue(File.Exists(RecordPath));
    }

    [TestMethod]
    public void KeepsTheRecordWhenAutoHideDoesNotChangeBack()
    {
        _fake.AutoHide = 1;
        WriteRecord(0);
        // A read-only file makes the change fail, like Explorer ignoring or reverting ABM_SETSTATE.
        File.SetAttributes(_fake.AutoHideFile, FileAttributes.ReadOnly);
        RestoreResult result;
        try
        {
            result = _restorer.Restore(NoDelay);
        }
        finally
        {
            File.SetAttributes(_fake.AutoHideFile, FileAttributes.Normal);
        }

        Assert.AreEqual(1u, _fake.AutoHide);
        Assert.IsNull(result.AutoHideRestoredTo);
        Assert.IsTrue(result.RecordKept);
        Assert.IsTrue(File.Exists(RecordPath));
    }

    [TestMethod]
    public void RestoreIsIdempotent()
    {
        _fake.AutoHide = 1;
        FakeTaskbar.Hide(_fake.Primary);
        WriteRecord(0);

        _restorer.Restore(NoDelay);
        Assert.IsTrue(FakeTaskbar.WaitUntilVisible(_fake.Primary, visible: true));
        var second = _restorer.Restore(NoDelay);
        Assert.AreEqual(0, second.WindowsShown);
        Assert.IsNull(second.AutoHideRestoredTo);
        Assert.AreEqual(0u, _fake.AutoHide);
    }

    [TestMethod]
    public void ShowHiddenWindowsNeverTouchesAutoHide()
    {
        _fake.AutoHide = 1;
        FakeTaskbar.Hide(_fake.Primary);
        WriteRecord(0);

        Assert.AreEqual(1, _restorer.ShowHiddenWindows());
        Assert.IsTrue(FakeTaskbar.WaitUntilVisible(_fake.Primary, visible: true));
        Assert.AreEqual(1u, _fake.AutoHide);
        Assert.IsTrue(File.Exists(RecordPath));
    }

    [TestMethod]
    public async Task RestoreAsyncRunsOffTheCallingThread()
    {
        _fake.AutoHide = 1;
        WriteRecord(0);

        var result = await _restorer.RestoreAsync(NoDelay, TimeSpan.FromSeconds(10));
        Assert.AreEqual(0u, result?.AutoHideRestoredTo);
    }

    // Read-only look at the real taskbar: finding windows never changes anything.
    [TestMethod]
    public void RealTargetFindsOnlyWindowsOwnedByTheShell()
    {
        var real = new TaskbarRestorer(TaskbarTarget.Real, recordPath: null, _log);
        var windows = real.FindWindows();
        if (windows.Count == 0)
        {
            Assert.Inconclusive("no taskbar in this session (Explorer isn't running)");
        }
        Interop.NativeMethods.GetWindowThreadProcessId(Interop.NativeMethods.GetShellWindow(), out var shellProcess);
        foreach (var window in windows)
        {
            Interop.NativeMethods.GetWindowThreadProcessId(window.Handle, out var owner);
            Assert.AreEqual(shellProcess, owner);
        }
        Assert.IsTrue(windows[0].IsPrimary);
        Assert.IsTrue(windows.Skip(1).All(window => !window.IsPrimary));
    }
}
