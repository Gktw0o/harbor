#pragma once

#include "core/geometry/rect.h"

namespace harbor::appbar
{

// Port: the dock's AppBar, a strip along the bottom of the screen that maximized windows stay out of
// (SHAppBarMessage). The calls core/appbar/appbar_state.h's RunStep makes; each may block on Explorer.
class IAppBar
{
  public:
    virtual ~IAppBar() = default;

    // ABM_NEW. False if Explorer refused (or isn't running).
    virtual bool Register() = 0;
    // ABM_REMOVE.
    virtual void Remove() = 0;
    // ABM_QUERYPOS: where Explorer would put this strip.
    virtual geometry::Rect Query(const geometry::Rect& proposed) = 0;
    // ABM_SETPOS: reserve it; returns what Explorer granted.
    virtual geometry::Rect Set(const geometry::Rect& proposed) = 0;
};

} // namespace harbor::appbar
