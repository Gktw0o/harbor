using Harbor.Core.Supervision;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class RestartPolicyTests
{
    private const int Crash = unchecked((int)0xC0000005);

    private static TimeSpan At(double seconds) => TimeSpan.FromSeconds(seconds);

    [TestMethod]
    public void RestartsAfterFailuresWithGrowingWaits()
    {
        var policy = new RestartPolicy(new RestartOptions());

        Assert.AreEqual(At(1), policy.Decide(Crash, At(0), sessionEnding: false).Delay);
        Assert.AreEqual(At(2), policy.Decide(DockExitCodes.KilledByWatchdog, At(5), sessionEnding: false).Delay);
        Assert.AreEqual(At(4), policy.Decide(1, At(10), sessionEnding: false).Delay);
    }

    [TestMethod]
    public void GivesUpAfterThreeRestartsInAMinute()
    {
        var policy = new RestartPolicy(new RestartOptions());
        for (var i = 0; i < 3; i++)
        {
            Assert.IsNotNull(policy.Decide(Crash, At(i), sessionEnding: false).Delay);
        }

        var fourth = policy.Decide(Crash, At(10), sessionEnding: false);
        Assert.IsNull(fourth.Delay);
        StringAssert.Contains(fourth.Reason, "giving up");

        // A minute after the first one, it may try once more; the two still within the minute keep the wait long.
        Assert.AreEqual(At(4), policy.Decide(Crash, At(60.5), sessionEnding: false).Delay);
        // Once a quiet minute has passed, it starts over with the shortest wait.
        Assert.AreEqual(At(1), policy.Decide(Crash, At(200), sessionEnding: false).Delay);
    }

    [TestMethod]
    public void NeverRestartsADockThatEndedOnPurposeOrCantRun()
    {
        var policy = new RestartPolicy(new RestartOptions());
        foreach (var exitCode in new[] { 0, DockExitCodes.AlreadyRunning, DockExitCodes.InvalidArguments, DockExitCodes.GraphicsUnavailable })
        {
            var decision = policy.Decide(exitCode, At(0), sessionEnding: false);
            Assert.IsNull(decision.Delay, $"0x{exitCode:X8}");
        }
        StringAssert.Contains(policy.Decide(DockExitCodes.GraphicsUnavailable, At(0), false).Reason, "graphics");
    }

    [TestMethod]
    public void NeverRestartsWhileWindowsSignsOut()
    {
        var policy = new RestartPolicy(new RestartOptions());
        Assert.IsNull(policy.Decide(Crash, At(0), sessionEnding: true).Delay);
        // That didn't count as a restart.
        Assert.AreEqual(At(1), policy.Decide(Crash, At(1), sessionEnding: false).Delay);
    }
}
