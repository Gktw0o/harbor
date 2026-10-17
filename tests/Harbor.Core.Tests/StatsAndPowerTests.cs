using System.Globalization;
using Harbor.Core.Power;
using Harbor.Core.Stats;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class StatsMathTests
{
    [TestMethod]
    public void CpuUseIsTheBusyShareOfTheTimeThatPassed()
    {
        // Kernel includes idle: 1,000 ticks passed, 250 of them idle.
        var before = new CpuTimes(Idle: 1000, Kernel: 3000, User: 2000);
        var after = new CpuTimes(Idle: 1250, Kernel: 3600, User: 2400);
        Assert.AreEqual(75.0, StatsMath.CpuUse(before, after)!.Value, 1e-9);
    }

    [TestMethod]
    public void CpuUseIsUnknownWithoutTimeOrWithCountersGoingBack()
    {
        var times = new CpuTimes(10, 20, 30);
        Assert.IsNull(StatsMath.CpuUse(times, times));
        Assert.IsNull(StatsMath.CpuUse(times, times with { User = 29 }));
        // Fully idle.
        Assert.AreEqual(0.0, StatsMath.CpuUse(times, new CpuTimes(110, 120, 30))!.Value);
    }

    [TestMethod]
    public void GpuUseIsTheBusiestEngineTypeSummedOverItsEngines()
    {
        var engines = new[]
        {
            ("pid_1_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 20.0),
            ("pid_2_luid_0x0_0x1_phys_0_eng_0_engtype_3D", 25.0),
            ("pid_1_luid_0x0_0x1_phys_0_eng_4_engtype_VideoDecode", 30.0),
            ("pid_3_luid_0x0_0x1_phys_0_eng_1_engtype_Copy", 5.0),
        };
        Assert.AreEqual(45.0, StatsMath.GpuUse(engines)!.Value);
        Assert.IsNull(StatsMath.GpuUse([]));
        Assert.AreEqual(100.0, StatsMath.GpuUse([("a_engtype_3D", 70.0), ("b_engtype_3D", 60.0)])!.Value);
    }

    [TestMethod]
    public void CoreUseInProcessorOrderWithoutTotals()
    {
        var processors = new[] { ("0,10", 5.0), ("0,2", 50.0), ("_Total", 30.0), ("0,_Total", 30.0), ("1,0", 120.0), ("0,0", 10.0) };
        CollectionAssert.AreEqual(new[] { 10.0, 50.0, 5.0, 100.0 }, StatsMath.CoreUse(processors).ToArray());
    }

    [TestMethod]
    public void SizesAndDurationsForPeople()
    {
        var culture = CultureInfo.InvariantCulture;
        Assert.AreEqual("512 MB", StatsMath.Size(512UL * 1024 * 1024, culture));
        Assert.AreEqual("12.3 GB", StatsMath.Size((ulong)(12.3 * 1024 * 1024 * 1024), culture));
        Assert.AreEqual("1.5 TB", StatsMath.Size((ulong)(1.5 * 1024 * 1024 * 1024 * 1024), culture));
        Assert.AreEqual("2 h 05 min", StatsMath.Duration(TimeSpan.FromMinutes(125)));
        Assert.AreEqual("45 min", StatsMath.Duration(TimeSpan.FromMinutes(45.9)));
        Assert.AreEqual(0.25, new DriveSpace("C:", 75, 100).UsedShare, 1e-9);
        Assert.AreEqual(0.0, new DriveSpace("X:", 0, 0).UsedShare);
    }
}

// The real source, read only: it must give the basics on any machine (CI's too) and never throw.
[TestClass]
public sealed class WindowsStatsSourceTests
{
    [TestMethod]
    public async Task ReadsTheBasicsFromWindows()
    {
        using var source = new WindowsStatsSource();
        var first = source.Read();
        Assert.IsNull(first.Cpu, "a CPU rate needs two readings");
        await Task.Delay(300);
        var second = source.Read();
        Assert.IsNotNull(second.Cpu);
        Assert.IsTrue(second.Cpu is >= 0 and <= 100, $"{second.Cpu}");
        Assert.IsTrue(second.MemoryTotal > 0 && second.MemoryUsed <= second.MemoryTotal, $"{second.MemoryUsed} of {second.MemoryTotal}");
        Assert.IsNotEmpty(second.Drives, "no fixed drive");
        // Every Windows has the processor counters, one instance per logical processor: another count means the
        // PDH items were misread (a wrong item size still parses the first one).
        Assert.HasCount(Environment.ProcessorCount, second.Cores, "per-processor readings");
        Assert.IsTrue(second.Cores.All(core => core is >= 0 and <= 100));
        // GPU counters and a battery depend on the machine: present or not, never wrong.
        Assert.IsTrue(second.Gpu is null or (>= 0 and <= 100));
        Assert.IsTrue(second.Battery is null || second.Battery.Percent is >= 0 and <= 100);
    }
}

[TestClass]
public sealed class StatsSamplerTests
{
    private sealed class CountingSource : IStatsSource
    {
        private int _reads;

        public int Reads => Volatile.Read(ref _reads);

        public bool Disposed { get; private set; }

        public StatsReading Read()
        {
            Interlocked.Increment(ref _reads);
            return StatsReading.Empty with { Cpu = 1 };
        }

        public void Dispose() => Disposed = true;
    }

    // Nothing runs while the island is closed: readings only between Start and Stop.
    [TestMethod]
    public async Task ReadsOnlyWhileStartedAndLetsTheSourceGoLater()
    {
        var source = new CountingSource();
        var opened = 0;
        using var sampler = new StatsSampler(() => { opened++; return source; }, new CapturingLog(), TimeSpan.FromMilliseconds(50), TimeSpan.FromMilliseconds(300));
        var readings = 0;
        Assert.IsFalse(sampler.SourceOpen);

        sampler.Start(_ => Interlocked.Increment(ref readings));
        Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(Volatile.Read(ref readings) >= 3), TimeSpan.FromSeconds(5)));
        sampler.Stop();
        var stoppedAt = source.Reads;
        await Task.Delay(200);
        Assert.IsLessThanOrEqualTo(stoppedAt + 1, source.Reads, "it kept reading after Stop");
        Assert.IsTrue(sampler.SourceOpen, "closed at once: reopening the island would reopen it");
        Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(!sampler.SourceOpen), TimeSpan.FromSeconds(5)), "the source stayed open");
        Assert.IsTrue(source.Disposed);
        Assert.AreEqual(1, opened);
    }

    [TestMethod]
    public async Task StartingAgainSoonKeepsTheSource()
    {
        var source = new CountingSource();
        var opened = 0;
        using var sampler = new StatsSampler(() => { opened++; return source; }, new CapturingLog(), TimeSpan.FromMilliseconds(50), TimeSpan.FromMilliseconds(400));
        sampler.Start(_ => { });
        Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(source.Reads >= 1), TimeSpan.FromSeconds(5)));
        sampler.Stop();
        sampler.Start(_ => { });
        await Task.Delay(700);
        Assert.IsTrue(sampler.SourceOpen);
        Assert.IsFalse(source.Disposed);
        Assert.AreEqual(1, opened);
    }

    [TestMethod]
    public async Task AFailingSourceStopsTheReadingsNotTheShell()
    {
        var log = new CapturingLog();
        using var sampler = new StatsSampler(() => throw new InvalidOperationException("no counters"), log, TimeSpan.FromMilliseconds(50));
        sampler.Start(_ => { });
        Assert.IsTrue(await TestDock.WaitUntilAsync(() => Task.FromResult(log.Contains("stats: reading stopped")), TimeSpan.FromSeconds(5)));
    }
}

[TestClass]
public sealed class PowerTests
{
    [TestMethod]
    public void EndingTheSessionOrThePcAsksFirst()
    {
        Assert.IsTrue(PowerPolicy.NeedsConfirmation(PowerAction.SignOut));
        Assert.IsTrue(PowerPolicy.NeedsConfirmation(PowerAction.Restart));
        Assert.IsTrue(PowerPolicy.NeedsConfirmation(PowerAction.ShutDown));
        Assert.IsFalse(PowerPolicy.NeedsConfirmation(PowerAction.Lock));
        Assert.IsFalse(PowerPolicy.NeedsConfirmation(PowerAction.Sleep));
        Assert.IsFalse(PowerPolicy.NeedsConfirmation(PowerAction.Hibernate));
    }

    [TestMethod]
    public void HibernateIsOfferedOnlyWhereItIsEnabled()
    {
        CollectionAssert.DoesNotContain(PowerPolicy.Offered(new PowerCapabilities(true, CanHibernate: false)).ToArray(), PowerAction.Hibernate);
        CollectionAssert.Contains(PowerPolicy.Offered(new PowerCapabilities(true, CanHibernate: true)).ToArray(), PowerAction.Hibernate);
        Assert.AreEqual(PowerAction.Lock, PowerPolicy.Offered(new PowerCapabilities(false, false))[0]);
    }

    // A test Shell's power actions only say what was asked.
    [TestMethod]
    public void TheRecordingActionsDoNothingButRecord()
    {
        var log = new CapturingLog();
        var power = new RecordingPowerActions(log);
        Assert.IsTrue(power.Run(PowerAction.ShutDown));
        CollectionAssert.AreEqual(new[] { PowerAction.ShutDown }, power.Asked.ToArray());
        Assert.IsTrue(log.Contains("Shut down asked (a test Shell: nothing done)"));
    }

    [TestMethod]
    public void ATestShellNeverGetsTheRealPowerActions()
    {
        using var dir = new TempDirectory();
        var (options, _) = ShellOptions.Parse(["--pipe-name", "harbor.test.power", "--data-dir", dir.Path, "--test-taskbar", "power"]);
        var power = ShellHost.PowerActionsFor(options!, new CapturingLog(), real: () => throw new AssertFailedException("the real power actions were made for a test Shell"));
        Assert.IsInstanceOfType<RecordingPowerActions>(power);
    }
}
