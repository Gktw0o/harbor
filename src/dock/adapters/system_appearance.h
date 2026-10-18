#pragma once

#include <windows.h>

#include <optional>

#include "core/island/appearance.h"

namespace harbor::adapters
{

// Reads the Windows settings the island follows: dark or light mode, transparency effects, energy saver and
// high contrast. Cheap; the dock reads them again on WM_SETTINGCHANGE, WM_THEMECHANGED, WM_SYSCOLORCHANGE and
// power-setting changes. Anything unreadable keeps its default. Windows 11's energy saver is only told in a
// notification (EnergySaverIn): energySaverStatus is its last value, the old battery saver flag counts too.
island::SystemAppearance ReadSystemAppearance(bool energySaverStatus) noexcept;

// A PBT_POWERSETTINGCHANGE's setting: whether energy saver is on, if it is GUID_ENERGY_SAVER_STATUS
// (docs/adr/0013); nullopt for any other setting or a malformed one.
std::optional<bool> EnergySaverIn(const POWERBROADCAST_SETTING& setting) noexcept;

} // namespace harbor::adapters
