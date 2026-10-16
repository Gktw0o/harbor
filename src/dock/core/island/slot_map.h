#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace harbor::island
{

// What each visible place on the island holds, left to right.
struct SlotMap
{
    enum class Kind
    {
        // The Start button (docs/adr/0015).
        Start,
        // One of the dock's slots (pinned items and running apps, DockModel order).
        Slot,
        // The "…" button: the slots from `slot` on are in its menu.
        Overflow,
        // The tray button, while Harbor hides the taskbar (docs/adr/0012).
        Tray,
    };
    struct Entry
    {
        Kind kind = Kind::Slot;
        // Kind::Slot: its slot; Kind::Overflow: the first slot behind it; otherwise 0.
        std::size_t slot = 0;
        friend bool operator==(const Entry&, const Entry&) = default;
    };

    std::vector<Entry> entries;

    // The place of the first entry of that kind.
    std::optional<std::size_t> IndexOf(Kind kind) const noexcept;
    // The place of a slot shown on the island; nullopt if it is behind the "…" button or out of range.
    std::optional<std::size_t> IndexOfSlot(std::size_t slot) const noexcept;
    // How many slots are shown on the island (not behind the "…" button).
    std::size_t SlotsShown() const noexcept;
    friend bool operator==(const SlotMap&, const SlotMap&) = default;
};

// The places for `visible` items (what fits the monitor): the Start button first and the tray button last
// when asked for, the slots in between; when the slots don't all fit, the last place left for them becomes
// the "…" button for the rest. With fewer than two places, the Start button comes before the tray button.
SlotMap ArrangeSlots(bool start, std::size_t slots, bool tray, std::size_t visible);

} // namespace harbor::island
