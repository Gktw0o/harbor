#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/windows/app_windows.h"

namespace harbor::island
{

enum class MenuCommand
{
    // The app's name at the top: does nothing.
    Title,
    // Bring this window forward.
    Window,
    // Start the app (another window if it runs).
    Launch,
    Pin,
    Unpin,
    // Close this window (the app's only one).
    CloseWindow,
    // Close each of the app's windows.
    CloseAll,
    // The overflow menu: do what clicking that dock item would.
    Item,
};

struct MenuItem
{
    MenuCommand command = MenuCommand::Title;
    std::string text;
    // MenuCommand::Window and CloseWindow: which.
    windows::WindowId window = 0;
    bool separatorAbove = false;
    bool enabled = true;
    // MenuCommand::Item: which dock item.
    std::size_t item = 0;
    friend bool operator==(const MenuItem&, const MenuItem&) = default;
};

// The overflow "…" menu: the items that didn't fit on the island, by name; names[i] is dock item first + i.
std::vector<MenuItem> BuildOverflowMenu(std::span<const std::string> names, std::size_t first);

struct MenuWindow
{
    windows::WindowId id = 0;
    // UTF-8; read when the menu opens, never logged.
    std::string title;
};

struct MenuFacts
{
    std::string name;
    // In the order first seen.
    std::vector<MenuWindow> windows;
    bool pinned = false;
    // It has something the Shell can pin (and so unpin).
    bool pinnable = false;
    // It has something to start.
    bool launchable = false;
};

// Window titles longer than this are cut.
inline constexpr std::size_t kMenuTitleCharacters = 60;

// The right-click menu for an item (docs/adr/0010), flat: the app's name; its windows by title; "New window"
// (or "Open" when nothing is open); "Pin to dock" or "Unpin from dock"; "Close window" or "Close all
// windows".
std::vector<MenuItem> BuildAppMenu(const MenuFacts& facts);

// text cut to at most maxCharacters code points, the last of them "…" when cut. Never splits a UTF-8
// sequence.
std::string Shorten(std::string_view text, std::size_t maxCharacters);

} // namespace harbor::island
