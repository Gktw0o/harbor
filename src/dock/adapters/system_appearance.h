#pragma once

#include "core/island/appearance.h"

namespace harbor::adapters
{

// Reads the Windows settings the island follows: dark or light mode, transparency effects, energy saver and
// high contrast. Cheap; the dock reads them again on WM_SETTINGCHANGE, WM_THEMECHANGED, WM_SYSCOLORCHANGE and
// power-setting changes. Anything unreadable keeps its default.
island::SystemAppearance ReadSystemAppearance() noexcept;

} // namespace harbor::adapters
