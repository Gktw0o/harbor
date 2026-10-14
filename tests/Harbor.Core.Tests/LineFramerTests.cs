using System.Text;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

// Same cases as tests/dock/line_framer_tests.cpp.
[TestClass]
public sealed class LineFramerTests
{
    private static string[] Push(LineFramer framer, string text) =>
        [.. framer.Push(Encoding.UTF8.GetBytes(text)).Select(line => Encoding.UTF8.GetString(line))];

    [TestMethod]
    public void ReturnsCompleteLine() => CollectionAssert.AreEqual(new[] { "abc" }, Push(new LineFramer(), "abc\n"));

    [TestMethod]
    public void HoldsPartialLineUntilTerminated()
    {
        var framer = new LineFramer();
        Assert.AreEqual(0, Push(framer, "ab").Length);
        Assert.AreEqual(0, Push(framer, "c").Length);
        CollectionAssert.AreEqual(new[] { "abcd" }, Push(framer, "d\nef"));
        CollectionAssert.AreEqual(new[] { "ef" }, Push(framer, "\n"));
    }

    [TestMethod]
    public void SplitsSeveralLinesInOnePush() =>
        CollectionAssert.AreEqual(new[] { "a", "", "b" }, Push(new LineFramer(), "a\n\nb\n"));

    [TestMethod]
    public void AcceptsLineOfExactlyMaxLength()
    {
        var framer = new LineFramer();
        var line = new string('x', LineFramer.MaxLineBytes);
        CollectionAssert.AreEqual(new[] { line }, Push(framer, line + "\n"));
        Assert.AreEqual(0, framer.DroppedLineCount);
    }

    [TestMethod]
    public void DropsOversizedLineAndResynchronises()
    {
        var framer = new LineFramer();
        var tooLong = new string('x', LineFramer.MaxLineBytes + 1);
        CollectionAssert.AreEqual(new[] { "next" }, Push(framer, tooLong + "\nnext\n"));
        Assert.AreEqual(1, framer.DroppedLineCount);
    }

    [TestMethod]
    public void DropsOversizedLineSpreadOverManyPushes()
    {
        var framer = new LineFramer();
        var chunk = new string('x', 1024);
        for (var i = 0; i < 100; i++)
        {
            Assert.AreEqual(0, Push(framer, chunk).Length);
        }
        CollectionAssert.AreEqual(new[] { "ok" }, Push(framer, "tail\nok\n"));
        Assert.AreEqual(1, framer.DroppedLineCount);
    }
}
