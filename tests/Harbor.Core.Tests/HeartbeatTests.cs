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
