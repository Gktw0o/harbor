#include "core/island/appearance.h"

namespace harbor::island
{

IslandStyle StyleFor(const SystemAppearance& appearance) noexcept
{
    if (appearance.highContrast)
    {
        // The user's high-contrast colors and nothing see-through.
        return {
            .backdrop = false,
            .fill = appearance.highContrastWindow,
            .border = appearance.highContrastText,
            .hover = appearance.highContrastHighlight,
            .placeholder = appearance.highContrastText,
            .dot = appearance.highContrastText,
            .attention = appearance.highContrastHighlight,
        };
    }
    // Like Windows' own flyouts: blur with a translucent tint, or the tint made opaque when transparency is
    // off (the raw backdrop brush would draw black then).
    const bool backdrop = appearance.transparency && !appearance.energySaver;
    const std::uint8_t alpha = backdrop ? 170 : 255;
    if (appearance.dark)
    {
        return {
            .backdrop = backdrop,
            .fill = {alpha, 32, 32, 32},
            .border = {48, 255, 255, 255},
            .hover = {40, 255, 255, 255},
            .placeholder = {255, 96, 96, 96},
            .dot = {204, 255, 255, 255},
            .attention = {110, 255, 176, 32},
        };
    }
    return {
        .backdrop = backdrop,
        .fill = {alpha, 243, 243, 243},
        .border = {32, 0, 0, 0},
        .hover = {24, 0, 0, 0},
        .placeholder = {255, 200, 200, 200},
        .dot = {178, 0, 0, 0},
        .attention = {96, 255, 140, 0},
    };
}

} // namespace harbor::island
