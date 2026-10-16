#pragma once

#include <windows.h>

#include "ports/app_bar.h"

namespace harbor::adapters
{

// The real AppBar: a bottom-edge strip registered with Explorer for window, which receives Explorer's
// notifications (ABN_*) as callbackMessage.
class Win32AppBar final : public appbar::IAppBar
{
  public:
    Win32AppBar(HWND window, UINT callbackMessage) noexcept;

    bool Register() override;
    void Remove() override;
    geometry::Rect Query(const geometry::Rect& proposed) override;
    geometry::Rect Set(const geometry::Rect& proposed) override;

  private:
    HWND m_window;
    UINT m_callbackMessage;
};

// Test docks: answers like Explorer would (the strip goes above the bottom of its monitor's work area, so
// above a visible taskbar) but reserves nothing, so tests never change the user's work area.
class TestAppBar final : public appbar::IAppBar
{
  public:
    bool Register() override;
    void Remove() override;
    geometry::Rect Query(const geometry::Rect& proposed) override;
    geometry::Rect Set(const geometry::Rect& proposed) override;
};

} // namespace harbor::adapters
