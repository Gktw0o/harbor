using Harbor.Core.Island;
using Harbor.Core.Ipc;

namespace Harbor.Core.Tests;

[TestClass]
public sealed class IslandModelTests
{
    private static readonly ScreenRect Monitor = new(0, 0, 1920, 1080);
    private static readonly DockStateMessage Shown = new(Monitor, Monitor, 96, FullScreen: false, Visible: true);
    private static readonly DockStateMessage FullScreen = Shown with { FullScreen = true };
    private static readonly DockStateMessage Panicked = Shown with { Visible = false };

    [TestMethod]
    public void StartsAsTheEmptyNotchEvenWithoutADock()
    {
        var model = new IslandModel();
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
        Assert.IsFalse(model.Open);
        Assert.IsNull(model.Dock);
    }

    [TestMethod]
    public void TheMouseGrowsItIntoTheBubbleAndAClickOpensIt()
    {
        var model = new IslandModel();
        model.OnPointer(true);
        Assert.AreEqual(IslandShape.Bubble, model.Shape);
        model.OnPointer(false);
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
        model.OnPointer(true);
        model.OnClick();
        Assert.AreEqual(IslandShape.Expanded, model.Shape);
        Assert.IsTrue(model.Open);
        // Open, the mouse leaving changes nothing.
        model.OnPointer(false);
        Assert.AreEqual(IslandShape.Expanded, model.Shape);
    }

    [TestMethod]
    public void TheHotkeyOpensAndClosesIt()
    {
        var model = new IslandModel();
        model.OnHotkey();
        Assert.AreEqual(IslandShape.Expanded, model.Shape);
        model.OnHotkey();
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
    }

    [TestMethod]
    public void DismissedItClosesToTheNotchEvenWithTheMouseOnIt()
    {
        var model = new IslandModel();
        model.OnPointer(true);
        model.OnClick();
        model.OnDismiss();
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
        // The next move over it grows it again.
        model.OnPointer(true);
        Assert.AreEqual(IslandShape.Bubble, model.Shape);
    }

    [TestMethod]
    public void AFullScreenAppHidesItUntilItEnds()
    {
        var model = new IslandModel();
        model.OnDockState(Shown);
        model.OnDockState(FullScreen);
        Assert.AreEqual(IslandShape.Hidden, model.Shape);
        model.OnPointer(true);
        model.OnClick();
        Assert.AreEqual(IslandShape.Hidden, model.Shape, "a click on a hidden island");
        model.OnDockState(Shown);
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
    }

    [TestMethod]
    public void OpenedOnPurposeItStaysOpenOverAFullScreenApp()
    {
        var model = new IslandModel();
        model.OnDockState(FullScreen);
        model.OnHotkey();
        Assert.AreEqual(IslandShape.Expanded, model.Shape);
        model.OnDockState(FullScreen with { Dpi = 144 });
        Assert.AreEqual(IslandShape.Expanded, model.Shape);
        model.OnDismiss();
        Assert.AreEqual(IslandShape.Hidden, model.Shape, "closed, it is behind the full-screen app again");
    }

    [TestMethod]
    public void ThePanicHotkeyTakesItAwayOpenOrNotAndTheHotkeyCantBringItBack()
    {
        var model = new IslandModel();
        model.OnHotkey();
        model.OnDockState(Panicked);
        Assert.AreEqual(IslandShape.Hidden, model.Shape);
        Assert.IsFalse(model.Open);
        model.OnHotkey();
        Assert.AreEqual(IslandShape.Hidden, model.Shape);
        model.OnDockState(Shown);
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
    }

    [TestMethod]
    public void WithoutADockItStaysOnThePrimaryMonitor()
    {
        var model = new IslandModel();
        model.OnDockState(FullScreen);
        model.OnDockState(null);
        Assert.AreEqual(IslandShape.Collapsed, model.Shape);
        Assert.IsNull(model.Dock);
    }
}

[TestClass]
public sealed class IslandPlacementTests
{
    [TestMethod]
    public void FlushWithTheTopEdgeCentredOnTheMonitor()
    {
        var monitor = new ScreenRect(0, 0, 1920, 1080);
        Assert.AreEqual(new ScreenRect(860, 0, 1060, 30), IslandPlacement.WindowFor(IslandShape.Collapsed, monitor, 96));
        Assert.AreEqual(new ScreenRect(800, 0, 1120, 44), IslandPlacement.WindowFor(IslandShape.Bubble, monitor, 96));
        Assert.AreEqual(new ScreenRect(470, 0, 1450, 420), IslandPlacement.WindowFor(IslandShape.Expanded, monitor, 96));
    }

    [TestMethod]
    public void ScaledByTheMonitorsDpiAndPlacedOnThatMonitor()
    {
        // A 200 % monitor left of and above the primary.
        var monitor = new ScreenRect(-2880, -360, 0, 1440);
        Assert.AreEqual(new ScreenRect(-1640, -360, -1240, -300), IslandPlacement.WindowFor(IslandShape.Collapsed, monitor, 192));
    }

    [TestMethod]
    public void ANarrowMonitorGetsAPanelThatFits()
    {
        // 1024×768 at 125 %: 980 DIP would be 1225 px.
        var monitor = new ScreenRect(0, 0, 1024, 768);
        var panel = IslandPlacement.WindowFor(IslandShape.Expanded, monitor, 120);
        Assert.AreEqual(1024 - 40, panel.Width);
        Assert.AreEqual(20, panel.Left);
        Assert.AreEqual(525, panel.Height);
        // And no taller than 80 % of a short one.
        Assert.AreEqual(400, IslandPlacement.WindowFor(IslandShape.Expanded, new ScreenRect(0, 0, 1280, 500), 96).Height);
    }

    [TestMethod]
    public void HiddenIsAnEmptyRectangleAtTheTop()
    {
        var hidden = IslandPlacement.WindowFor(IslandShape.Hidden, new ScreenRect(0, 0, 1920, 1080), 96);
        Assert.AreEqual(0, hidden.Width);
        Assert.AreEqual(0, hidden.Height);
        Assert.AreEqual(960, hidden.Left);
    }
}
