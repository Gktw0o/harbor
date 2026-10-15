#include <gtest/gtest.h>

#include "core/windows/eligibility.h"

namespace harbor::windows
{
namespace
{

WindowFacts Plain()
{
    return {.visible = true, .rect = {100, 100, 900, 700}, .className = "Chrome_WidgetWin_1"};
}

TEST(Eligibility, AVisibleUnownedWindowIsAnAppWindow)
{
    EXPECT_TRUE(IsAppWindow(Plain()));
}

TEST(Eligibility, HiddenOrEmptyWindowsAreNot)
{
    auto hidden = Plain();
    hidden.visible = false;
    EXPECT_FALSE(IsAppWindow(hidden));
    auto empty = Plain();
    empty.rect = {100, 100, 100, 700};
    EXPECT_FALSE(IsAppWindow(empty));
}

TEST(Eligibility, ToolOwnedAndNeverActivatedWindowsAreNot)
{
    auto tool = Plain();
    tool.toolWindow = true;
    EXPECT_FALSE(IsAppWindow(tool));
    auto owned = Plain();
    owned.owned = true;
    EXPECT_FALSE(IsAppWindow(owned));
    auto noActivate = Plain();
    noActivate.noActivate = true;
    EXPECT_FALSE(IsAppWindow(noActivate));
}

TEST(Eligibility, AppWindowStyleBringsThemBackIn)
{
    auto owned = Plain();
    owned.owned = true;
    owned.appWindow = true;
    EXPECT_TRUE(IsAppWindow(owned));
    auto tool = Plain();
    tool.toolWindow = true;
    tool.appWindow = true;
    EXPECT_TRUE(IsAppWindow(tool));
    auto noActivate = Plain();
    noActivate.noActivate = true;
    noActivate.appWindow = true;
    EXPECT_TRUE(IsAppWindow(noActivate));
}

TEST(Eligibility, ShellCloakedOrOtherDesktopWindowsAreNot)
{
    auto shell = Plain();
    shell.cloak = Cloak::Shell;
    EXPECT_FALSE(IsAppWindow(shell));
    auto inherited = Plain();
    inherited.cloak = Cloak::Inherited;
    EXPECT_FALSE(IsAppWindow(inherited));
    auto elsewhere = Plain();
    elsewhere.onOtherDesktop = true;
    EXPECT_FALSE(IsAppWindow(elsewhere));
}

TEST(Eligibility, AWindowItsAppCloakedStays)
{
    auto cloaked = Plain();
    cloaked.cloak = Cloak::App;
    EXPECT_TRUE(IsAppWindow(cloaked));
}

TEST(Eligibility, ExplorersShellWindowsAreNotButItsFolderWindowsAre)
{
    for (const char* name :
         {"Progman", "WorkerW", "Shell_TrayWnd", "Shell_SecondaryTrayWnd", "Shell_OOBEProxy"})
    {
        auto facts = Plain();
        facts.className = name;
        facts.explorer = true;
        EXPECT_FALSE(IsAppWindow(facts)) << name;
    }
    auto folder = Plain();
    folder.className = "CabinetWClass";
    folder.explorer = true;
    EXPECT_TRUE(IsAppWindow(folder));
    // Another app's window with a taskbar's class name is that app's business (docs/adr/0006).
    auto lookalike = Plain();
    lookalike.className = "Shell_TrayWnd";
    EXPECT_TRUE(IsAppWindow(lookalike));
}

TEST(Eligibility, GhostsAndHarborsOwnWindowsAreNot)
{
    auto ghost = Plain();
    ghost.className = "Ghost";
    EXPECT_FALSE(IsAppWindow(ghost));
    auto harbor = Plain();
    harbor.harbor = true;
    EXPECT_FALSE(IsAppWindow(harbor));
}

} // namespace
} // namespace harbor::windows
