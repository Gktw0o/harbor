#pragma once

#include <cstdint>

namespace harbor::island
{

struct Color
{
    std::uint8_t a = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(const Color&, const Color&) = default;
};

// The Windows settings the island follows (CLAUDE.md rule 7).
struct SystemAppearance
{
    // Settings > Personalization > Colors: "Choose your mode" for Windows (not apps).
    bool dark = true;
    // "Transparency effects".
    bool transparency = true;
    // Energy saver (battery saver) is on: Windows turns transparency off for its own surfaces too.
    bool energySaver = false;
    bool highContrast = false;
    // High-contrast colors (only used while highContrast): window background, text and selection.
    Color highContrastWindow{255, 0, 0, 0};
    Color highContrastText{255, 255, 255, 255};
    Color highContrastHighlight{255, 26, 235, 255};
    friend bool operator==(const SystemAppearance&, const SystemAppearance&) = default;
};

struct IslandStyle
{
    // Blur what's behind the island (the host backdrop) under a translucent fill; otherwise the fill is
    // opaque.
    bool backdrop = false;
    Color fill;
    // A hairline around the island, so it stands out from a background of the same color.
    Color border;
    // Behind the item under the mouse.
    Color hover;
    // An item without an icon.
    Color placeholder;
    // The running dot under an app's icon.
    Color dot;
    // Behind an app that asked for attention.
    Color attention;
    friend bool operator==(const IslandStyle&, const IslandStyle&) = default;
};

IslandStyle StyleFor(const SystemAppearance& appearance) noexcept;

} // namespace harbor::island
