using Harbor.Core.Logging;

namespace Harbor.Core.Tests;

// Same cases as tests/dock/log_line_tests.cpp: the dock and Shell logs share one format.
[TestClass]
public sealed class LogLineTests
{
    [TestMethod]
    public void FormatsTimestampLevelAndMessage() => Assert.AreEqual(
        "2026-10-14T15:30:00.123Z [info] dock started\n",
        LogLine.Format(new DateTime(2026, 10, 14, 15, 30, 0, 123, DateTimeKind.Utc), LogLevel.Info, "dock started"));

    [TestMethod]
    public void PadsEveryField() => Assert.AreEqual(
        "2026-01-02T03:04:05.006Z [warn] x\n",
        LogLine.Format(new DateTime(2026, 1, 2, 3, 4, 5, 6, DateTimeKind.Utc), LogLevel.Warning, "x"));

    [TestMethod]
    public void EscapesLineBreaksSoEachEntryIsOneLine() => Assert.AreEqual(
        "2026-10-14T15:30:00.000Z [error] a\\r\\nb\\nc\n",
        LogLine.Format(new DateTime(2026, 10, 14, 15, 30, 0, DateTimeKind.Utc), LogLevel.Error, "a\r\nb\nc"));

    [TestMethod]
    public void KeepsUnicodeText() => Assert.AreEqual(
        "2026-10-14T15:30:00.000Z [info] Ünï ğ\n",
        LogLine.Format(new DateTime(2026, 10, 14, 15, 30, 0, DateTimeKind.Utc), LogLevel.Info, "Ünï ğ"));
}
