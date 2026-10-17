using Harbor.Core.Media;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class MediaTests
{
    private static readonly DateTime Reported = new(2026, 10, 17, 12, 0, 0, DateTimeKind.Utc);

    private static MediaState Song(bool playing, TimeSpan? position, TimeSpan? duration) =>
        new("Title", "Artist", playing, true, true, true, position, duration, Reported);

    [TestMethod]
    public void PlayingItMovesOnFromWhereItWasReported()
    {
        var song = Song(playing: true, TimeSpan.FromSeconds(30), TimeSpan.FromMinutes(3));
        Assert.AreEqual(TimeSpan.FromSeconds(42), song.PositionAt(Reported.AddSeconds(12)));
        Assert.AreEqual(42.0 / 180, song.ProgressAt(Reported.AddSeconds(12))!.Value, 1e-9);
    }

    [TestMethod]
    public void PausedItStaysAndItNeverRunsPastTheEnd()
    {
        Assert.AreEqual(TimeSpan.FromSeconds(30), Song(playing: false, TimeSpan.FromSeconds(30), TimeSpan.FromMinutes(3)).PositionAt(Reported.AddMinutes(5)));
        Assert.AreEqual(TimeSpan.FromMinutes(3), Song(playing: true, TimeSpan.FromSeconds(170), TimeSpan.FromMinutes(3)).PositionAt(Reported.AddSeconds(30)));
        // A clock that went back a little.
        Assert.AreEqual(TimeSpan.Zero, Song(playing: true, TimeSpan.Zero, TimeSpan.FromMinutes(3)).PositionAt(Reported.AddSeconds(-2)));
    }

    [TestMethod]
    public void WithoutATimelineThereIsNoProgress()
    {
        Assert.IsNull(Song(playing: true, null, null).PositionAt(Reported));
        Assert.IsNull(Song(playing: true, TimeSpan.FromSeconds(5), null).ProgressAt(Reported));
        Assert.IsNull(Song(playing: true, TimeSpan.FromSeconds(5), TimeSpan.Zero).ProgressAt(Reported));
        // A live stream: it goes on with no end.
        Assert.AreEqual(TimeSpan.FromSeconds(15), Song(playing: true, TimeSpan.FromSeconds(5), null).PositionAt(Reported.AddSeconds(10)));
    }

    [TestMethod]
    public void TimesForPeople()
    {
        Assert.AreEqual("0:07", MediaState.Clock(TimeSpan.FromSeconds(7)));
        Assert.AreEqual("3:45", MediaState.Clock(TimeSpan.FromSeconds(225)));
        Assert.AreEqual("1:02:03", MediaState.Clock(new TimeSpan(1, 2, 3)));
    }

    [TestMethod]
    public void ATestShellNeverGetsTheMachinesMedia()
    {
        using var dir = new TempDirectory();
        var (options, _) = ShellOptions.Parse(["--pipe-name", "harbor.test.media", "--data-dir", dir.Path, "--test-taskbar", "media"]);
        using var media = ShellHost.MediaSourceFor(options!, real: () => throw new AssertFailedException("the machine's media source was made for a test Shell"));
        Assert.IsInstanceOfType<NoMediaSource>(media);
    }

    [TestMethod]
    public async Task ATestShellsMediaIsNothing()
    {
        using var media = new NoMediaSource();
        await media.StartAsync();
        Assert.IsNull(media.Current);
        Assert.IsFalse(await media.PlayPauseAsync());
    }
}
