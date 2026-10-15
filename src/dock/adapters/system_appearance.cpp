#include "adapters/system_appearance.h"

#include <windows.h>

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

island::SystemAppearance ReadSystemAppearance() noexcept
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

    SYSTEM_POWER_STATUS power{};
    appearance.energySaver = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;

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

} // namespace harbor::adapters
