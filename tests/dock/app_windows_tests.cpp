#include <gtest/gtest.h>

#include "core/windows/app_windows.h"

namespace harbor::windows
{
namespace
{

TrackedWindow Window(WindowId id, std::string_view exe, std::uint64_t started, std::string_view aumid = {})
{
    return {.id = id,
            .app = IdentityOf(aumid, exe, 1000),
            .aumid = std::string(aumid),
            .exe = std::string(exe),
            .processStarted = started};
}

std::vector<WindowId> Ids(const AppWindows::App& app)
{
    std::vector<WindowId> ids;
    for (const auto& window : app.windows)
    {
        ids.push_back(window.id);
    }
    return ids;
}

TEST(AppIdentity, PrefersTheAppUserModelIdThenThePathThenTheProcess)
{
    EXPECT_EQ(IdentityOf("Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", "C:\\WT.exe", 7),
              (AppIdentity{AppIdentity::Kind::Aumid, "microsoft.windowsterminal_8wekyb3d8bbwe!app"}));
    EXPECT_EQ(IdentityOf("", "C:\\Program Files\\App.EXE", 7),
              (AppIdentity{AppIdentity::Kind::Exe, "c:\\program files\\app.exe"}));
    EXPECT_EQ(IdentityOf("", "", 7), (AppIdentity{AppIdentity::Kind::Process, "7"}));
}

TEST(AppIdentity, LowersAsciiOnly)
{
    // "İ" and "Ä" stay: case-folding beyond ASCII would need the OS's tables, and paths read back the same.
    EXPECT_EQ(LowerAscii("C:\\İçerik\\ÄPP.exe"), "c:\\İçerik\\Äpp.exe");
}

TEST(AppWindows, GroupsWindowsByAppInTheOrderTheyWereSeen)
{
    AppWindows apps;
    apps.Upsert(Window(30, "C:\\a.exe", 100));
    apps.Upsert(Window(10, "C:\\A.EXE", 100));
    ASSERT_EQ(apps.Apps().size(), 1u);
    EXPECT_EQ(Ids(apps.Apps()[0]), (std::vector<WindowId>{30, 10}));
    EXPECT_EQ(apps.Apps()[0].exe, "C:\\a.exe");
    EXPECT_EQ(apps.AppOf(10), apps.Apps()[0].id);
    EXPECT_FALSE(apps.AppOf(99));
}

TEST(AppWindows, OrdersAppsByWhenTheirProcessStarted)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\late.exe", 300));
    apps.Upsert(Window(2, "C:\\early.exe", 100));
    apps.Upsert(Window(3, "C:\\tie.exe", 300));
    ASSERT_EQ(apps.Apps().size(), 3u);
    EXPECT_EQ(apps.Apps()[0].exe, "C:\\early.exe");
    EXPECT_EQ(apps.Apps()[1].exe, "C:\\late.exe");
    EXPECT_EQ(apps.Apps()[2].exe, "C:\\tie.exe");
}

TEST(AppWindows, AnAppKeepsItsPlaceWhenItsFirstProcessGoes)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    apps.Upsert(Window(2, "C:\\b.exe", 200));
    // A second process of a, started after b.
    apps.Upsert(Window(3, "C:\\a.exe", 300));
    apps.Remove(1);
    ASSERT_EQ(apps.Apps().size(), 2u);
    EXPECT_EQ(apps.Apps()[0].exe, "C:\\a.exe");
}

TEST(AppWindows, ALateAppUserModelIdMovesTheWindowKeepingItsPlace)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\chrome.exe", 100));
    apps.Upsert(Window(2, "C:\\chrome.exe", 100));
    apps.Activated(2);
    const auto exeApp = apps.Apps()[0].id;
    const auto seen = apps.Apps()[0].windows[1].seen;

    apps.Upsert(Window(2, "C:\\chrome.exe", 100, "Chrome.Profile1"));
    ASSERT_EQ(apps.Apps().size(), 2u);
    const auto moved = apps.AppOf(2);
    ASSERT_TRUE(moved);
    EXPECT_NE(*moved, exeApp);
    const auto& profile = apps.Apps()[1].id == *moved ? apps.Apps()[1] : apps.Apps()[0];
    EXPECT_EQ(profile.identity.kind, AppIdentity::Kind::Aumid);
    EXPECT_EQ(profile.windows[0].seen, seen);
    EXPECT_EQ(profile.windows[0].active, 1u);

    // The last window of the exe group follows: the group goes.
    apps.Upsert(Window(1, "C:\\chrome.exe", 100, "Chrome.Profile1"));
    ASSERT_EQ(apps.Apps().size(), 1u);
    EXPECT_EQ(Ids(apps.Apps()[0]), (std::vector<WindowId>{1, 2}));
}

TEST(AppWindows, TheLastWindowGoingTakesTheAppAlong)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    apps.Remove(1);
    EXPECT_TRUE(apps.Apps().empty());
    EXPECT_FALSE(apps.AppOf(1));
}

TEST(AppWindows, AnAppThatComesBackGetsItsIdBackButAProcessDoesNot)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    const auto first = apps.Apps()[0].id;
    apps.Remove(1);
    apps.Upsert(Window(2, "C:\\A.exe", 500));
    EXPECT_EQ(apps.Apps()[0].id, first);

    // Neither an AppUserModelID nor a path: process ids are reused, so the id isn't.
    apps.Upsert(Window(3, "", 600));
    const auto process = *apps.AppOf(3);
    apps.Remove(3);
    apps.Upsert(Window(4, "", 700));
    EXPECT_NE(*apps.AppOf(4), process);
}

TEST(AppWindows, RemembersWhichWindowWasInFrontLast)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    apps.Upsert(Window(2, "C:\\a.exe", 100));
    apps.Activated(2);
    apps.Activated(1);
    apps.Activated(77);
    const auto& windows = apps.Apps()[0].windows;
    EXPECT_GT(windows[0].active, windows[1].active);
    EXPECT_GT(windows[1].active, 0u);
}

TEST(AppWindows, AFlashLastsUntilTheAppComesForward)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\chat.exe", 100));
    apps.Upsert(Window(2, "C:\\chat.exe", 100));
    apps.Upsert(Window(3, "C:\\other.exe", 200));
    apps.Flash(2);
    apps.Flash(99);
    EXPECT_TRUE(apps.Apps()[0].windows[1].attention);
    const auto revision = apps.Revision();
    apps.Flash(2);
    EXPECT_EQ(apps.Revision(), revision);

    // Another app coming forward changes nothing; this app's other window coming forward clears it.
    apps.Activated(3);
    EXPECT_TRUE(apps.Apps()[0].windows[1].attention);
    apps.Activated(1);
    EXPECT_FALSE(apps.Apps()[0].windows[1].attention);
}

TEST(AppWindows, OnlyChangesBumpTheRevision)
{
    AppWindows apps;
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    const auto revision = apps.Revision();
    apps.Upsert(Window(1, "C:\\a.exe", 100));
    apps.Remove(42);
    apps.Activated(42);
    EXPECT_EQ(apps.Revision(), revision);

    auto minimized = Window(1, "C:\\a.exe", 100);
    minimized.minimized = true;
    apps.Upsert(minimized);
    EXPECT_GT(apps.Revision(), revision);
    EXPECT_TRUE(apps.Apps()[0].windows[0].minimized);
}

} // namespace
} // namespace harbor::windows
