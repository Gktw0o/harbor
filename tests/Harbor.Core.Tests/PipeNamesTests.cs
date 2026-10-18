using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// Same cases as tests/dock/pipe_name_tests.cpp: both sides must produce identical names.
[TestClass]
public sealed class PipeNamesTests
{
    [TestMethod]
    public void ControlChannelIncludesSessionId() =>
        Assert.AreEqual("harbor.control.3", PipeNames.For(PipeNames.ControlChannel, 3));

    [TestMethod]
    public void SessionZero() => Assert.AreEqual("harbor.stats.0", PipeNames.For("stats", 0));

    [TestMethod]
    public void MaxSessionId() =>
        Assert.AreEqual("harbor.control.4294967295", PipeNames.For(PipeNames.ControlChannel, uint.MaxValue));

    [TestMethod]
    public void GeneratedNamesAreValid()
    {
        Assert.IsTrue(PipeNames.IsValid(PipeNames.For(PipeNames.ControlChannel, uint.MaxValue)));
        Assert.IsTrue(PipeNames.IsValid("harbor.e2e.0123456789abcdef0123456789abcdef"));
    }

    [TestMethod]
    public void RejectsNamesOutsideHarborNamespace()
    {
        foreach (var name in new[] { "", "harbor.", "other.control.1", "harbor.Control.1", @"harbor.control\..\x", "harbor.control 1", "harbor." + new string('a', 201) })
        {
            Assert.IsFalse(PipeNames.IsValid(name), name);
        }
    }
}
