#include "core/island/menu_model.h"

namespace harbor::island
{
namespace
{

bool IsContinuation(char byte) noexcept
{
    return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
}

} // namespace

std::string Shorten(std::string_view text, std::size_t maxCharacters)
{
    if (maxCharacters == 0)
    {
        return {};
    }
    // Where each of the first maxCharacters code points starts.
    std::size_t characters = 0;
    std::size_t lastStart = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (IsContinuation(text[i]))
        {
            continue;
        }
        if (characters == maxCharacters)
        {
            // Too long: keep maxCharacters - 1 code points and the ellipsis.
            return std::string(text.substr(0, lastStart)) + "\xE2\x80\xA6";
        }
        lastStart = i;
        ++characters;
    }
    return std::string(text);
}

std::vector<MenuItem> BuildAppMenu(const MenuFacts& facts)
{
    std::vector<MenuItem> menu;
    menu.push_back({.command = MenuCommand::Title, .text = facts.name, .enabled = false});
    for (std::size_t i = 0; i < facts.windows.size(); ++i)
    {
        const auto& window = facts.windows[i];
        menu.push_back(
            {.command = MenuCommand::Window,
             .text = Shorten(window.title.empty() ? facts.name : window.title, kMenuTitleCharacters),
             .window = window.id,
             .separatorAbove = i == 0});
    }
    bool groupStarted = false;
    if (facts.launchable)
    {
        menu.push_back({.command = MenuCommand::Launch,
                        .text = facts.windows.empty() ? "Open" : "New window",
                        .separatorAbove = true});
        groupStarted = true;
    }
    if (facts.pinned || facts.pinnable)
    {
        menu.push_back({.command = facts.pinned ? MenuCommand::Unpin : MenuCommand::Pin,
                        .text = facts.pinned ? "Unpin from dock" : "Pin to dock",
                        .separatorAbove = !groupStarted});
    }
    if (facts.windows.size() == 1)
    {
        menu.push_back({.command = MenuCommand::CloseWindow,
                        .text = "Close window",
                        .window = facts.windows[0].id,
                        .separatorAbove = true});
    }
    else if (facts.windows.size() > 1)
    {
        menu.push_back(
            {.command = MenuCommand::CloseAll, .text = "Close all windows", .separatorAbove = true});
    }
    return menu;
}

} // namespace harbor::island
