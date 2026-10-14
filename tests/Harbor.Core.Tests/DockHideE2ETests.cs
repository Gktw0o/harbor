using System.Diagnostics;
using System.Runtime.InteropServices;
using Harbor.Core.Ipc;
using Microsoft.Win32.SafeHandles;

namespace Harbor.Core.Tests;

// The dock's own hide rules end to end, with a bare pipe server standing in for Harbor Shell, so the tests can
// do what the real watchdog never would (go quiet while connected, ask a dock it doesn't hold to hide).
[TestClass]
public sealed partial class DockHideE2ETests
{
    [TestMethod]
    public async Task WhenPingsStopTheDockRestoresTheTaskbar()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        try
        {
            await server.WaitForAsync(s => s.LastPongSeq is not null);
            Assert.IsTrue(server.Server.TrySend(new SetTaskbar(Hidden: true)));
            await server.WaitForMessageAsync<TaskbarState>(s => s.Hidden);
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(taskbar.Primary, visible: false));

            // Still connected, but the watchdog is silent: after the ping timeout (1.5 s for test docks) the
            // dock gives the taskbar back by itself.
            server.PausePinging();
            await server.WaitForMessageAsync<TaskbarState>(s => !s.Hidden);
            Assert.IsTrue(FakeTaskbar.WaitUntilVisible(taskbar.Primary, visible: true));
            // The windows come back at once; auto-hide follows on the dock's explorer worker.
            var deadline = Stopwatch.StartNew();
            while (taskbar.AutoHide != 0 && deadline.Elapsed < RunningServer.Timeout)
            {
                await Task.Delay(20);
            }
            Assert.AreEqual(0u, taskbar.AutoHide);
            StringAssert.Contains(await TestDock.ReadLogAsync(dataDir.Path), "watchdog went quiet");
        }
        finally
        {
            TestDock.KillQuietly(dock.Id);
        }
    }

    // A job that kills its processes when its last handle closes could end the dock and Harbor Shell together,
    // leaving nothing to restore the taskbar: the dock refuses to hide in one.
    [TestMethod]
    public async Task ADockInAKillOnCloseJobRefusesToHide()
    {
        await using var server = new RunningServer($"harbor.e2e.{Guid.NewGuid():N}", TimeSpan.FromMilliseconds(100));
        using var dataDir = new TempDirectory();
        using var taskbar = new FakeTaskbar(dataDir.Path);
        using var dock = TestDock.Start(server.PipeName, dataDir.Path, taskbar);
        // Closing the job's handle at the end ends the dock, too.
        using var job = CreateKillOnCloseJob();
        try
        {
            Assert.IsTrue(AssignProcessToJobObject(job, dock.SafeHandle), $"error {Marshal.GetLastPInvokeError()}");
            await server.WaitForAsync(s => s.LastPongSeq is not null);

            Assert.IsTrue(server.Server.TrySend(new SetTaskbar(Hidden: true)));
            await server.WaitForMessageAsync<TaskbarState>(s => s.Blocked == TaskbarBlock.KillOnCloseJob);
            Assert.IsTrue(FakeTaskbar.IsVisible(taskbar.Primary));
        }
        finally
        {
            TestDock.KillQuietly(dock.Id);
        }
    }

    private static SafeFileHandle CreateKillOnCloseJob()
    {
        var job = CreateJobObject(0, null);
        Assert.IsFalse(job.IsInvalid);
        var limits = new ExtendedLimitInformation { LimitFlags = JobObjectLimitKillOnJobClose };
        Assert.IsTrue(SetInformationJobObject(job, JobObjectExtendedLimitInformation, ref limits, (uint)Marshal.SizeOf<ExtendedLimitInformation>()));
        return job;
    }

    private const int JobObjectExtendedLimitInformation = 9;
    private const uint JobObjectLimitKillOnJobClose = 0x2000;

    // JOBOBJECT_EXTENDED_LIMIT_INFORMATION; only LimitFlags is set.
    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimitInformation
    {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public nuint MinimumWorkingSetSize;
        public nuint MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public nuint Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
        public nuint ProcessMemoryLimit;
        public nuint JobMemoryLimit;
        public nuint PeakProcessMemoryUsed;
        public nuint PeakJobMemoryUsed;
    }

    [LibraryImport("kernel32.dll", EntryPoint = "CreateJobObjectW", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    private static partial SafeFileHandle CreateJobObject(nint attributes, string? name);

    [LibraryImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetInformationJobObject(SafeFileHandle job, int informationClass, ref ExtendedLimitInformation information, uint length);

    [LibraryImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool AssignProcessToJobObject(SafeFileHandle job, SafeProcessHandle process);
}
