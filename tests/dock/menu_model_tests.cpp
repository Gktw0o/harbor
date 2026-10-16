#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/island/menu_model.h"

namespace harbor::island
{
namespace
{

using Command = MenuCommand;

std::vector<Command> Commands(const std::vector<MenuItem>& menu)
{
    std::vector<Command> commands;
    for (const auto& item : menu)
    {
        commands.push_back(item.command);
    }
    return commands;
}

TEST(MenuModel, ARunningPinnedAppWithSeveralWindows)
{
    const auto menu = BuildAppMenu({.name = "Terminal",
                                    .windows = {{.id = 1, .title = "PowerShell"}, {.id = 2, .title = ""}},
                                    .pinned = true,
                                    .pinnable = true,
                                    .launchable = true});
    EXPECT_EQ(Commands(menu), (std::vector<Command>{Command::Title, Command::Window, Command::Window,
                                                    Command::Launch, Command::Unpin, Command::CloseAll}));
    EXPECT_EQ(menu[0], (MenuItem{.command = Command::Title, .text = "Terminal", .enabled = false}));
    EXPECT_EQ(
        menu[1],
        (MenuItem{.command = Command::Window, .text = "PowerShell", .window = 1, .separatorAbove = true}));
    // A window without a title goes by the app's name.
    EXPECT_EQ(menu[2].text, "Terminal");
    EXPECT_FALSE(menu[2].separatorAbove);
    EXPECT_EQ(menu[3].text, "New window");
    EXPECT_TRUE(menu[3].separatorAbove);
    EXPECT_EQ(menu[4].text, "Unpin from dock");
    EXPECT_FALSE(menu[4].separatorAbove);
    EXPECT_EQ(menu[5].text, "Close all windows");
}

TEST(MenuModel, AnUnpinnedAppWithOneWindow)
{
    const auto menu = BuildAppMenu({.name = "Paint",
                                    .windows = {{.id = 9, .title = "Untitled - Paint"}},
                                    .pinnable = true,
                                    .launchable = true});
    EXPECT_EQ(Commands(menu), (std::vector<Command>{Command::Title, Command::Window, Command::Launch,
                                                    Command::Pin, Command::CloseWindow}));
    EXPECT_EQ(menu[3].text, "Pin to dock");
    EXPECT_EQ(
        menu.back(),
        (MenuItem{
            .command = Command::CloseWindow, .text = "Close window", .window = 9, .separatorAbove = true}));
}

TEST(MenuModel, APinnedAppThatIsntRunning)
{
    const auto menu = BuildAppMenu({.name = "Editor", .pinned = true, .pinnable = true, .launchable = true});
    EXPECT_EQ(Commands(menu), (std::vector<Command>{Command::Title, Command::Launch, Command::Unpin}));
    EXPECT_EQ(menu[1].text, "Open");
}

TEST(MenuModel, AnAppTheShellCantPinOrStart)
{
    const auto menu = BuildAppMenu({.name = "Tool", .windows = {{.id = 3, .title = "Tool"}}});
    EXPECT_EQ(Commands(menu), (std::vector<Command>{Command::Title, Command::Window, Command::CloseWindow}));
}

TEST(MenuModel, PinStartsItsOwnGroupWithoutLaunch)
{
    const auto menu = BuildAppMenu({.name = "Tool", .pinnable = true});
    EXPECT_EQ(Commands(menu), (std::vector<Command>{Command::Title, Command::Pin}));
    EXPECT_TRUE(menu[1].separatorAbove);
}

TEST(MenuModel, LongTitlesAreCut)
{
    const std::string title(100, 'x');
    const auto menu = BuildAppMenu({.name = "App", .windows = {{.id = 1, .title = title}}});
    EXPECT_EQ(menu[1].text, std::string(59, 'x') + "\xE2\x80\xA6");
}

TEST(MenuModel, TheOverflowMenuListsTheItemsThatDidntFit)
{
    const std::vector<std::string> names{"Paint", "Notepad"};
    EXPECT_EQ(BuildOverflowMenu(names, 30),
              (std::vector<MenuItem>{{.command = Command::Item, .text = "Paint", .item = 30},
                                     {.command = Command::Item, .text = "Notepad", .item = 31}}));
}

TEST(MenuModel, ShortenCountsCodePointsAndNeverSplitsOne)
{
    EXPECT_EQ(Shorten("abc", 3), "abc");
    EXPECT_EQ(Shorten("abcd", 3), "ab\xE2\x80\xA6");
    // "ğ" is two bytes and "𝔸" four: three code points fit as they are.
    EXPECT_EQ(Shorten("a\xC4\x9F\xF0\x9D\x94\xB8", 3), "a\xC4\x9F\xF0\x9D\x94\xB8");
    EXPECT_EQ(Shorten("\xF0\x9D\x94\xB8\xF0\x9D\x94\xB8\xF0\x9D\x94\xB8\xF0\x9D\x94\xB8", 3),
              "\xF0\x9D\x94\xB8\xF0\x9D\x94\xB8\xE2\x80\xA6");
    EXPECT_EQ(Shorten("abc", 0), "");
}

} // namespace
} // namespace harbor::island
