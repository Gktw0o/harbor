#include <gtest/gtest.h>
#include <map>
#include <vector>

#include "app/window_tracker.h"

namespace harbor::dock
{
namespace
{

using namespace std::chrono_literals;
using windows::WindowEvent;
using windows::WindowId;
using Kind = WindowEvent::Kind;

class FakeWindowSystem final : public windows::IWindowSystem
{
  public:
    struct Window
    {
        windows::WindowFacts facts;
        windows::WindowApp app;
        bool minimized = false;
        WindowId owner = 0;
    };

    // An app window of exe, started at started.
    void Add(WindowId id, std::string exe, std::uint64_t started = 100, std::string className = "App")
    {
        windows[id] = {
            .facts = {.visible = true, .rect = {0, 0, 100, 100}, .className = std::move(className)},
            .app = {.exe = std::move(exe), .processId = 7, .processStarted = started}};
        zOrder.insert(zOrder.begin(), id);
    }

    bool Start(std::function<void()>) override
    {
        return true;
    }
    void Stop() noexcept override
    {
    }
    std::vector<WindowEvent> TakeEvents(bool& overflowed) override
    {
        overflowed = false;
        return {};
    }
    std::vector<WindowId> TopLevelWindows() override
    {
        return zOrder;
    }
    std::optional<windows::WindowFacts> FactsOf(WindowId id) override
    {
        const auto found = windows.find(id);
        return found != windows.end() ? std::optional(found->second.facts) : std::nullopt;
    }
    std::optional<windows::WindowApp> AppOf(WindowId id) override
    {
        ++appReads;
        const auto found = windows.find(id);
        return found != windows.end() ? std::optional(found->second.app) : std::nullopt;
    }
    bool IsMinimized(WindowId id) override
    {
        return windows.contains(id) && windows[id].minimized;
    }
    WindowId RootOwner(WindowId id) override
    {
        while (windows.contains(id) && windows[id].owner != 0)
        {
            id = windows[id].owner;
        }
        return id;
    }

    std::map<WindowId, Window> windows;
    // Front to back.
    std::vector<WindowId> zOrder;
    int appReads = 0;
};

std::vector<WindowId> AllWindows(const windows::AppWindows& apps)
{
    std::vector<WindowId> ids;
    for (const auto& app : apps.Apps())
    {
        for (const auto& window : app.windows)
        {
            ids.push_back(window.id);
        }
    }
    return ids;
}

TEST(WindowTracker, ARescanFindsAppWindowsAndTakesRecencyFromTheZOrder)
{
    FakeWindowSystem system;
    system.Add(2, "C:\\a.exe");
    system.Add(1, "C:\\a.exe");
    system.Add(3, "C:\\tool.exe");
    system.windows[3].facts.toolWindow = true;
    WindowTracker tracker(system, "");
    tracker.Rescan(0ms);

    ASSERT_EQ(tracker.Apps().Apps().size(), 1u);
    const auto& windows = tracker.Apps().Apps()[0].windows;
    ASSERT_EQ(windows.size(), 2u);
    // 1 is in front of 2.
    const auto& one = windows[0].id == 1 ? windows[0] : windows[1];
    const auto& two = windows[0].id == 2 ? windows[0] : windows[1];
    EXPECT_GT(one.active, two.active);
}

TEST(WindowTracker, EventsAddUpdateAndRemoveWindows)
{
    FakeWindowSystem system;
    WindowTracker tracker(system, "");
    tracker.Rescan(0ms);

    system.Add(1, "C:\\a.exe");
    tracker.OnEvents({{Kind::Changed, 1}}, 0ms);
    EXPECT_EQ(AllWindows(tracker.Apps()), (std::vector<WindowId>{1}));

    system.windows[1].minimized = true;
    tracker.OnEvents({{Kind::Changed, 1}}, 0ms);
    EXPECT_TRUE(tracker.Apps().Apps()[0].windows[0].minimized);

    system.windows[1].facts.cloak = windows::Cloak::Shell;
    tracker.OnEvents({{Kind::Changed, 1}}, 0ms);
    EXPECT_TRUE(AllWindows(tracker.Apps()).empty());

    system.windows[1].facts.cloak = windows::Cloak::None;
    tracker.OnEvents({{Kind::Changed, 1}}, 0ms);
    tracker.OnEvents({{Kind::Destroyed, 1}, {Kind::Destroyed, 99}}, 0ms);
    EXPECT_TRUE(AllWindows(tracker.Apps()).empty());
}

TEST(WindowTracker, ATestDockFollowsOnlyItsTestWindows)
{
    FakeWindowSystem system;
    system.Add(1, "C:\\users-app.exe", 100, "Chrome_WidgetWin_1");
    system.Add(2, "C:\\test.exe", 100, "Harbor.Test.abc.App");
    system.Add(3, "C:\\test.exe", 100, "Harbor.Test.xyz.App");
    WindowTracker tracker(system, "Harbor.Test.abc.App");
    tracker.Rescan(0ms);
    tracker.OnEvents({{Kind::Changed, 1}, {Kind::Foreground, 1}}, 0ms);
    EXPECT_EQ(AllWindows(tracker.Apps()), (std::vector<WindowId>{2}));
    // The user's windows are never even asked which app they are.
    EXPECT_EQ(system.appReads, 1);
}

TEST(WindowTracker, ALateAppUserModelIdIsPickedUpOnASecondLook)
{
    FakeWindowSystem system;
    WindowTracker tracker(system, "");
    tracker.Rescan(1000ms);
    system.Add(1, "C:\\chrome.exe");
    tracker.OnEvents({{Kind::Changed, 1}}, 1000ms);
    EXPECT_EQ(tracker.NextTimer(), 1500ms);

    system.windows[1].app.aumid = "Chrome.Profile";
    tracker.OnTimer(1499ms);
    EXPECT_EQ(tracker.Apps().Apps()[0].identity.kind, windows::AppIdentity::Kind::Exe);
    tracker.OnTimer(1500ms);
    EXPECT_EQ(tracker.Apps().Apps()[0].identity.kind, windows::AppIdentity::Kind::Aumid);
    EXPECT_EQ(tracker.NextTimer(), 3000ms);
    tracker.OnTimer(3000ms);
    EXPECT_FALSE(tracker.NextTimer());
}

TEST(WindowTracker, TheForegroundCountsForItsRootOwnerAndReadsItsAppAgain)
{
    FakeWindowSystem system;
    system.Add(1, "C:\\a.exe");
    system.Add(2, "C:\\a.exe");
    // A dialog owned by 2 (owned windows aren't app windows themselves).
    system.Add(5, "C:\\a.exe");
    system.windows[5].facts.owned = true;
    system.windows[5].owner = 2;
    WindowTracker tracker(system, "");
    tracker.Rescan(0ms);

    system.windows[2].app.aumid = "A.Late";
    tracker.OnEvents({{Kind::Foreground, 5}}, 10ms);
    ASSERT_EQ(tracker.Apps().Apps().size(), 2u);
    const auto& late =
        tracker.Apps().Apps()[1].windows[0].id == 2 ? tracker.Apps().Apps()[1] : tracker.Apps().Apps()[0];
    EXPECT_EQ(late.identity.kind, windows::AppIdentity::Kind::Aumid);
    const auto& other =
        &late == &tracker.Apps().Apps()[0] ? tracker.Apps().Apps()[1] : tracker.Apps().Apps()[0];
    EXPECT_GT(late.windows[0].active, other.windows[0].active);
}

TEST(WindowTracker, KnowsWhenAWindowIsBeingMovedOrSized)
{
    FakeWindowSystem system;
    WindowTracker tracker(system, "");
    tracker.OnEvents({{Kind::MoveSizeStart, 4}}, 0ms);
    EXPECT_TRUE(tracker.MovingOrSizing());
    tracker.OnEvents({{Kind::MoveSizeEnd, 4}}, 0ms);
    EXPECT_FALSE(tracker.MovingOrSizing());
    // A window destroyed while being moved doesn't leave the flag set.
    tracker.OnEvents({{Kind::MoveSizeStart, 4}, {Kind::Destroyed, 4}}, 0ms);
    EXPECT_FALSE(tracker.MovingOrSizing());
}

TEST(WindowTracker, ARescanForgetsWindowsThatWentQuietly)
{
    FakeWindowSystem system;
    system.Add(1, "C:\\a.exe");
    system.Add(2, "C:\\b.exe");
    WindowTracker tracker(system, "");
    tracker.Rescan(0ms);
    system.windows.erase(2);
    system.zOrder = {1};
    tracker.Rescan(0ms);
    EXPECT_EQ(AllWindows(tracker.Apps()), (std::vector<WindowId>{1}));
}

} // namespace
} // namespace harbor::dock
