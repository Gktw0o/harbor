// The power-setting notification the dock follows for Windows 11's energy saver (docs/adr/0013).

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "adapters/system_appearance.h"

namespace harbor::adapters
{
namespace
{

// A POWERBROADCAST_SETTING as Windows sends it: the header, then DataLength bytes.
std::vector<std::byte> Setting(const GUID& guid, DWORD value, DWORD length = sizeof(DWORD))
{
    std::vector<std::byte> buffer(offsetof(POWERBROADCAST_SETTING, Data) + sizeof(DWORD));
    auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(buffer.data());
    setting->PowerSetting = guid;
    setting->DataLength = length;
    std::memcpy(setting->Data, &value, sizeof(value));
    return buffer;
}

const POWERBROADCAST_SETTING& As(const std::vector<std::byte>& buffer)
{
    return *reinterpret_cast<const POWERBROADCAST_SETTING*>(buffer.data());
}

TEST(SystemAppearance, EnergySaverStatusOnOrOff)
{
    EXPECT_EQ(EnergySaverIn(As(Setting(GUID_ENERGY_SAVER_STATUS, ENERGY_SAVER_OFF))), false);
    EXPECT_EQ(EnergySaverIn(As(Setting(GUID_ENERGY_SAVER_STATUS, ENERGY_SAVER_STANDARD))), true);
    EXPECT_EQ(EnergySaverIn(As(Setting(GUID_ENERGY_SAVER_STATUS, ENERGY_SAVER_HIGH_SAVINGS))), true);
}

TEST(SystemAppearance, OtherSettingsSayNothingAboutEnergySaver)
{
    // The old battery saver notification is read from GetSystemPowerStatus instead.
    EXPECT_EQ(EnergySaverIn(As(Setting(GUID_POWER_SAVING_STATUS, 1))), std::nullopt);
    EXPECT_EQ(EnergySaverIn(As(Setting(GUID_ENERGY_SAVER_STATUS, ENERGY_SAVER_STANDARD, 1))), std::nullopt);
}

} // namespace
} // namespace harbor::adapters
