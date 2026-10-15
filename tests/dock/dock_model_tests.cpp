#include <gtest/gtest.h>
#include <vector>

#include "core/island/dock_model.h"

namespace harbor::island
{
namespace
{

using namespace std::chrono_literals;
using ipc::DockItem;
using ipc::DockItemKind;
using windows::AppIdentity;

const std::vector<DockItem> kPins{
    {DockItemKind::App, "Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", "Terminal", "t.bgra"},
    {DockItemKind::Path, "C:\\Tools\\Editor.exe", "Editor", std::nullopt},
    {DockItemKind::Path, "C:\\Users\\me\\Desktop\\Game.lnk", "Game", "g.bgra"},
};

RunningApp App(windows::AppId id, AppIdentity::Kind kind, std::string key)
{
    return {.id = id, .identity = {kind, std::move(key)}, .fallbackName = "fallback", .seen = 0ms};
}

TEST(DockModel, PinsComeFirstThenTheOtherRunningAppsInTheOrderGiven)
{
    std::vector<RunningApp> apps{App(7, AppIdentity::Kind::Exe, "c:\\b.exe"),
                                 App(3, AppIdentity::Kind::Exe, "c:\\a.exe")};
    apps[0].info = AppInfo{.name = "B", .icon = "b.bgra"};
    apps[1].info = AppInfo{.name = "A"};
    const auto model = BuildDockModel(kPins, apps, 1000ms);

    EXPECT_EQ(model.pinned, 3u);
    ASSERT_EQ(model.slots.size(), 5u);
    EXPECT_EQ(model.slots[0],
              (DockSlot{.kind = DockSlot::Kind::Pin, .pin = 0, .name = "Terminal", .icon = "t.bgra"}));
    EXPECT_EQ(model.slots[3], (DockSlot{.kind = DockSlot::Kind::App,
                                        .apps = {7},
                                        .name = "B",
                                        .icon = "b.bgra",
                                        .marks = {.running = true}}));
    EXPECT_EQ(model.slots[4].name, "A");
    EXPECT_FALSE(model.rebuildAt);
}

TEST(DockModel, APinnedAppThatRunsGetsADotNotASecondItem)
{
    // The pin's AppUserModelID, without case: no wait for the Shell.
    const std::vector<RunningApp> apps{
        App(1, AppIdentity::Kind::Aumid, "microsoft.windowsterminal_8wekyb3d8bbwe!app")};
    const auto model = BuildDockModel(kPins, apps, 0ms);
    ASSERT_EQ(model.slots.size(), 3u);
    EXPECT_EQ(model.slots[0].apps, (std::vector<windows::AppId>{1}));
    EXPECT_TRUE(model.slots[0].marks.running);
    EXPECT_FALSE(model.slots[1].marks.running);
}

TEST(DockModel, APathPinIsItsExecutable)
{
    const std::vector<RunningApp> apps{App(1, AppIdentity::Kind::Exe, "c:\\tools\\editor.exe")};
    EXPECT_EQ(BuildDockModel(kPins, apps, 0ms).slots[1].apps, (std::vector<windows::AppId>{1}));
    // An AppUserModelID that happens to equal a path pin's target is another app.
    const std::vector<RunningApp> aumid{App(1, AppIdentity::Kind::Aumid, "c:\\tools\\editor.exe")};
    EXPECT_EQ(BuildDockModel(kPins, aumid, 1000ms).slots.size(), 4u);
}

TEST(DockModel, TheShellCanSayWhichPinAnAppIs)
{
    // A shortcut's game: only the Shell knows the shortcut leads to it.
    auto game = App(5, AppIdentity::Kind::Exe, "d:\\games\\game.exe");
    game.info =
        AppInfo{.name = "Game", .pinned = PinRef{DockItemKind::Path, "C:\\Users\\me\\Desktop\\GAME.lnk"}};
    const std::vector<RunningApp> apps{game};
    const auto model = BuildDockModel(kPins, apps, 0ms);
    ASSERT_EQ(model.slots.size(), 3u);
    EXPECT_EQ(model.slots[2].apps, (std::vector<windows::AppId>{5}));
}

TEST(DockModel, APinCanStandForSeveralApps)
{
    auto second = App(2, AppIdentity::Kind::Exe, "c:\\tools\\editor-helper.exe");
    second.info = AppInfo{.name = "Editor", .pinned = PinRef{DockItemKind::Path, "C:\\Tools\\Editor.exe"}};
    second.attention = true;
    const std::vector<RunningApp> apps{App(1, AppIdentity::Kind::Exe, "c:\\tools\\editor.exe"), second};
    const auto model = BuildDockModel(kPins, apps, 0ms);
    const auto& slot = model.slots[1];
    EXPECT_EQ(slot.apps, (std::vector<windows::AppId>{1, 2}));
    EXPECT_EQ(slot.marks, (ItemMarks{.running = true, .attention = true}));
}

TEST(DockModel, AnAppWaitsForTheShellThenAppearsUnderItsFallbackName)
{
    auto app = App(9, AppIdentity::Kind::Exe, "c:\\new.exe");
    app.seen = 1000ms;
    const std::vector<RunningApp> apps{app};

    const auto waiting = BuildDockModel(kPins, apps, 1100ms);
    EXPECT_EQ(waiting.slots.size(), 3u);
    EXPECT_EQ(waiting.rebuildAt, 1250ms);

    const auto shown = BuildDockModel(kPins, apps, 1250ms);
    ASSERT_EQ(shown.slots.size(), 4u);
    EXPECT_EQ(shown.slots[3].name, "fallback");
    EXPECT_FALSE(shown.slots[3].icon);
    EXPECT_FALSE(shown.rebuildAt);
}

TEST(DockModel, AttentionMarksTheItem)
{
    auto app = App(4, AppIdentity::Kind::Exe, "c:\\chat.exe");
    app.info = AppInfo{.name = "Chat"};
    app.attention = true;
    const std::vector<RunningApp> apps{app};
    EXPECT_TRUE(BuildDockModel({}, apps, 0ms).slots[0].marks.attention);
}

TEST(DockModel, NoPinsNoDivider)
{
    auto app = App(4, AppIdentity::Kind::Exe, "c:\\chat.exe");
    app.info = AppInfo{.name = "Chat"};
    const std::vector<RunningApp> apps{app};
    const auto model = BuildDockModel({}, apps, 0ms);
    EXPECT_EQ(model.pinned, 0u);
    EXPECT_EQ(model.slots.size(), 1u);
}

TEST(DockModel, FallbackNames)
{
    EXPECT_EQ(FallbackName("", "C:\\Program Files\\Editor\\Code.EXE"), "Code");
    EXPECT_EQ(FallbackName("Some.App", "C:\\x\\tool"), "tool");
    EXPECT_EQ(FallbackName("Some.App", ""), "Some.App");
    EXPECT_EQ(FallbackName("", ""), "App");
    EXPECT_EQ(FallbackName("", "C:\\dir\\.exe"), ".exe");
}

} // namespace
} // namespace harbor::island
