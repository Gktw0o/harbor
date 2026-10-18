using Harbor.Core.Supervision;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class HeartbeatTests
{
    [TestMethod]
    public void AnsweredPingsKeepItHealthy()
    {
        var heartbeat = new Heartbeat(missedLimit: 3);
        for (var seq = 1u; seq <= 10; seq++)
        {
            heartbeat.Sent(seq);
            heartbeat.Answered(seq);
        }
        Assert.AreEqual(0, heartbeat.Missed);
        Assert.IsFalse(heartbeat.IsHung);
    }

    [TestMethod]
    public void CountsUnansweredPingsInARow()
    {
        var heartbeat = new Heartbeat(missedLimit: 3);
        heartbeat.Sent(1);
        heartbeat.Sent(2);
        heartbeat.Sent(3);
        Assert.AreEqual(2, heartbeat.Missed);
        Assert.IsFalse(heartbeat.IsHung);

        heartbeat.Sent(4);
        Assert.IsTrue(heartbeat.IsHung);
    }

    [TestMethod]
    public void AnAnswerResetsTheCount()
    {
        var heartbeat = new Heartbeat(missedLimit: 3);
        heartbeat.Sent(1);
        heartbeat.Sent(2);
        heartbeat.Sent(3);
        heartbeat.Answered(3);
        heartbeat.Sent(4);
        Assert.AreEqual(0, heartbeat.Missed);
    }

    [TestMethod]
    public void OnlyAnAnswerToTheLatestPingCounts()
    {
        var heartbeat = new Heartbeat(missedLimit: 3);
        heartbeat.Sent(1);
        heartbeat.Sent(2);
        heartbeat.Answered(1);
        heartbeat.Sent(3);
        Assert.AreEqual(2, heartbeat.Missed);
    }

    // Counting pings, not time: after a sleep, the first ping out and its answer leave nothing missed.
    [TestMethod]
    public void ATimeGapAloneIsNotAMiss()
    {
        var heartbeat = new Heartbeat(missedLimit: 1);
        heartbeat.Sent(1);
        heartbeat.Answered(1);
        heartbeat.Sent(2);
        Assert.IsFalse(heartbeat.IsHung);
    }

    // A dock that just connected may still be setting up on its UI thread, which answers the pings.
    [TestMethod]
    public void ADockStillStartingGetsItsStartupAllowance()
    {
        var heartbeat = new Heartbeat(missedLimit: 2, firstAnswerLimit: 5);
        for (var seq = 1u; seq <= 5; seq++)
        {
            heartbeat.Sent(seq);
        }
        Assert.AreEqual(4, heartbeat.Missed);
        Assert.IsFalse(heartbeat.IsHung);
        heartbeat.Sent(6);
        Assert.IsTrue(heartbeat.IsHung);
    }

    [TestMethod]
    public void AfterItsFirstAnswerTheUsualLimitCounts()
    {
        var heartbeat = new Heartbeat(missedLimit: 2, firstAnswerLimit: 5);
        heartbeat.Sent(1);
        heartbeat.Answered(1);
        heartbeat.Sent(2);
        heartbeat.Sent(3);
        Assert.IsFalse(heartbeat.IsHung);
        heartbeat.Sent(4);
        Assert.IsTrue(heartbeat.IsHung);
    }

    [TestMethod]
    public void ANewConnectionGetsTheAllowanceAgain()
    {
        var heartbeat = new Heartbeat(missedLimit: 2, firstAnswerLimit: 5);
        heartbeat.Sent(1);
        heartbeat.Answered(1);
        heartbeat.Reset();
        for (var seq = 2u; seq <= 5; seq++)
        {
            heartbeat.Sent(seq);
        }
        Assert.AreEqual(3, heartbeat.Missed);
        Assert.IsFalse(heartbeat.IsHung);
    }

    [TestMethod]
    public void TheAllowanceIsNeverShorterThanTheUsualLimit()
    {
        var heartbeat = new Heartbeat(missedLimit: 3, firstAnswerLimit: 1);
        heartbeat.Sent(1);
        heartbeat.Sent(2);
        heartbeat.Sent(3);
        Assert.IsFalse(heartbeat.IsHung);
        heartbeat.Sent(4);
        Assert.IsTrue(heartbeat.IsHung);
    }

    // The real watchdog (1 s pings) is unchanged: 5 pings before the first answer as after it. Test supervisors
    // (100 ms pings) give a starting dock the same 5 s.
    [TestMethod]
    public void TheWatchdogsStartupAllowanceIsFiveSeconds()
    {
        var real = new DockSupervisorOptions();
        Assert.AreEqual(real.MissedPongLimit, real.FirstAnswerLimit);
        Assert.AreEqual(5, real.FirstAnswerLimit);
        Assert.AreEqual(50, new DockSupervisorOptions { PingInterval = TimeSpan.FromMilliseconds(100) }.FirstAnswerLimit);
    }

    [TestMethod]
    public void ResetForgetsTheOutstandingPing()
    {
        var heartbeat = new Heartbeat(missedLimit: 2);
        heartbeat.Sent(1);
        heartbeat.Sent(2);
        heartbeat.Reset();
        heartbeat.Sent(3);
        Assert.AreEqual(0, heartbeat.Missed);
    }
}
