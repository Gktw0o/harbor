#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <vector>

#include "core/island/appearance.h"
#include "core/island/menu_model.h"

namespace harbor::adapters
{

// The dock's popup menus (docs/adr/0010): flat Win32 menus (keyboard, dismissal and screen readers come with
// them), owner-drawn in the island's colors, or plain system menus in high contrast. Screen readers read the
// owner-drawn items through MSAAMENUINFO; the keyboard selects them by their first letter (WM_MENUCHAR). One
// menu at a time, on the UI thread.
class PopupMenu
{
  public:
    struct Look
    {
        island::IslandStyle style;
        unsigned dpi = USER_DEFAULT_SCREEN_DPI;
        bool highContrast = false;
    };

    // Shows items with the menu's bottom at anchor (screen coordinates), never over avoid (the island), and
    // returns the index of the chosen item; nullopt if none was. Runs the menu's modal loop, during which
    // owner's window procedure keeps getting messages and must pass WM_MEASUREITEM, WM_DRAWITEM and
    // WM_MENUCHAR to the functions below. owner takes the foreground while the menu is open (or it wouldn't
    // close when clicking elsewhere); afterwards the window that had it gets it back.
    static std::optional<std::size_t> Show(HWND owner, const std::vector<island::MenuItem>& items,
                                           POINT anchor, const RECT& avoid, const Look& look);

    // True if it was for the open menu.
    static bool OnMeasureItem(MEASUREITEMSTRUCT& measure) noexcept;
    static bool OnDrawItem(const DRAWITEMSTRUCT& draw) noexcept;
    // WM_MENUCHAR: the next item whose text starts with the typed letter; nullopt for DefWindowProc.
    static std::optional<LRESULT> OnMenuChar(WPARAM wParam, LPARAM lParam) noexcept;
};

} // namespace harbor::adapters
