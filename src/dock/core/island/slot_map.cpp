#include "core/island/slot_map.h"

#include <algorithm>

namespace harbor::island
{

std::optional<std::size_t> SlotMap::IndexOf(Kind kind) const noexcept
{
    const auto found =
        std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.kind == kind; });
    return found != entries.end() ? std::optional(static_cast<std::size_t>(found - entries.begin()))
                                  : std::nullopt;
}

std::optional<std::size_t> SlotMap::IndexOfSlot(std::size_t slot) const noexcept
{
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [&](const Entry& e) { return e.kind == Kind::Slot && e.slot == slot; });
    return found != entries.end() ? std::optional(static_cast<std::size_t>(found - entries.begin()))
                                  : std::nullopt;
}

std::size_t SlotMap::SlotsShown() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(entries.begin(), entries.end(), [](const Entry& e) { return e.kind == Kind::Slot; }));
}

SlotMap ArrangeSlots(bool start, std::size_t slots, bool tray, std::size_t visible)
{
    SlotMap map;
    std::size_t left = visible;
    const bool showStart = start && left > 0;
    left -= showStart ? 1 : 0;
    const bool showTray = tray && left > 0;
    left -= showTray ? 1 : 0;

    if (showStart)
    {
        map.entries.push_back({SlotMap::Kind::Start});
    }
    if (slots <= left)
    {
        for (std::size_t slot = 0; slot < slots; ++slot)
        {
            map.entries.push_back({SlotMap::Kind::Slot, slot});
        }
    }
    else if (left > 0)
    {
        for (std::size_t slot = 0; slot + 1 < left; ++slot)
        {
            map.entries.push_back({SlotMap::Kind::Slot, slot});
        }
        map.entries.push_back({SlotMap::Kind::Overflow, left - 1});
    }
    if (showTray)
    {
        map.entries.push_back({SlotMap::Kind::Tray});
    }
    return map;
}

} // namespace harbor::island
