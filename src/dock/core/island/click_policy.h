#pragma once

#include <optional>
#include <span>

#include "core/windows/app_windows.h"

namespace harbor::island
{

struct ClickAction
{
    enum class Kind
    {
        // Nothing of the app is open: start it.
        Launch,
        // Bring window forward (restoring it first if it is minimized).
        Activate,
        // Minimize window.
        Minimize,
    };
    Kind kind = Kind::Launch;
    windows::WindowId window = 0;
    bool restore = false;
    friend bool operator==(const ClickAction&, const ClickAction&) = default;
};

// What a click on an item does (docs/adr/0010). windows: the item's windows, in any order (a pinned item may
// stand for several apps); foreground: the root owner of the window in front when the click came.
// - The item has no windows: start it.
// - None of its windows is in front: the one in front most recently comes forward.
// - One of them is: the next one in the order they were first seen comes forward (after the last, the
//   first), or, if it has only one, it is minimized.
ClickAction DecideClick(std::span<const windows::AppWindows::Window> windows,
                        std::optional<windows::WindowId> foreground);

} // namespace harbor::island
