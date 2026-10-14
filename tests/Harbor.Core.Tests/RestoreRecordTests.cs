using System.Text;
using Harbor.Core.Taskbar;

namespace Harbor.Core.Tests;

// Same cases as tests/dock/restore_record_tests.cpp. The shared fixtures in contracts/state cover the
// schema; these cover what JSON Schema can't express.
[TestClass]
public sealed class RestoreRecordTests
{
    [TestMethod]
    public void RoundTrips()
    {
        var record = new RestoreRecord(0, "2026-10-14T15:30:00Z");
        var encoded = RestoreRecord.Encode(record);
        Assert.AreEqual("""{"v":1,"stateBefore":0,"createdUtc":"2026-10-14T15:30:00Z"}""", Encoding.UTF8.GetString(encoded));
        Assert.IsTrue(RestoreRecord.TryDecode(encoded, out var decoded));
        Assert.AreEqual(record, decoded);
    }

    [TestMethod]
    public void RejectsDuplicateKeys() => Assert.IsFalse(RestoreRecord.TryDecode(
        """{"v":1,"stateBefore":1,"stateBefore":0,"createdUtc":"2026-10-14T15:30:00Z"}"""u8.ToArray(), out _));

    [TestMethod]
    public void RejectsEmptyInput() => Assert.IsFalse(RestoreRecord.TryDecode(Array.Empty<byte>(), out _));

    [TestMethod]
    public void TimestampShape()
    {
        Assert.IsTrue(RestoreRecord.IsUtcTimestamp("2026-10-14T15:30:00Z"));
        Assert.IsFalse(RestoreRecord.IsUtcTimestamp("2026-10-14T15:30:00.000Z"));
        Assert.IsFalse(RestoreRecord.IsUtcTimestamp("2026-10-14T15:30:00+00:00"));
        Assert.IsFalse(RestoreRecord.IsUtcTimestamp("2026-1O-14T15:30:00Z"));
        Assert.IsFalse(RestoreRecord.IsUtcTimestamp(""));
    }
}
