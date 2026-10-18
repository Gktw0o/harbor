#include "adapters/system_appearance.h"

#include <windows.h>

#include <cstring>
#include <optional>

namespace harbor::adapters
{
namespace
{

constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

std::optional<DWORD> ReadPersonalizeValue(const wchar_t* name) noexcept
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
        ERROR_SUCCESS)
    {
        return std::nullopt;
    }
    return value;
}

island::Color FromSysColor(int index) noexcept
{
    const COLORREF color = GetSysColor(index);
    return {255, GetRValue(color), GetGValue(color), GetBValue(color)};
}

} // namespace

island::SystemAppearance ReadSystemAppearance(bool energySaverStatus) noexcept
{
    island::SystemAppearance appearance;
    if (const auto light = ReadPersonalizeValue(L"SystemUsesLightTheme"))
    {
        appearance.dark = *light == 0;
    }
    if (const auto transparency = ReadPersonalizeValue(L"EnableTransparency"))
    {
        appearance.transparency = *transparency != 0;
    }

    // Windows 11's energy saver leaves the battery saver flag alone (M4 spike): either one counts.
    SYSTEM_POWER_STATUS power{};
    appearance.energySaver =
        energySaverStatus || (GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1);

    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0)
    {
        appearance.highContrast = true;
        appearance.highContrastWindow = FromSysColor(COLOR_WINDOW);
        appearance.highContrastText = FromSysColor(COLOR_WINDOWTEXT);
        appearance.highContrastHighlight = FromSysColor(COLOR_HIGHLIGHT);
    }
    return appearance;
}

std::optional<bool> EnergySaverIn(const POWERBROADCAST_SETTING& setting) noexcept
{
    if (!IsEqualGUID(setting.PowerSetting, GUID_ENERGY_SAVER_STATUS) || setting.DataLength != sizeof(DWORD))
    {
        return std::nullopt;
    }
    // ENERGY_SAVER_OFF, _STANDARD or _HIGH_SAVINGS: the host backdrop draws black under both of the last two.
    DWORD status = 0;
    std::memcpy(&status, setting.Data, sizeof(status));
    return status != ENERGY_SAVER_OFF;
}

} // namespace harbor::adapters
