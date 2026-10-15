#include <gtest/gtest.h>

#include "core/island/appearance.h"

namespace harbor::island
{
namespace
{

TEST(Appearance, BlursBehindATranslucentTintByDefault)
{
    const auto style = StyleFor({.dark = true});
    EXPECT_TRUE(style.backdrop);
    EXPECT_EQ(style.fill, (Color{170, 32, 32, 32}));
}

TEST(Appearance, FollowsLightMode)
{
    const auto style = StyleFor({.dark = false});
    EXPECT_EQ(style.fill, (Color{170, 243, 243, 243}));
    EXPECT_NE(style.hover, StyleFor({.dark = true}).hover);
}

// The raw backdrop brush draws black without transparency effects: the fill must be opaque instead.
TEST(Appearance, GoesSolidWithoutTransparencyOrOnEnergySaver)
{
    for (const auto& appearance :
         {SystemAppearance{.transparency = false}, SystemAppearance{.energySaver = true}})
    {
        const auto style = StyleFor(appearance);
        EXPECT_FALSE(style.backdrop);
        EXPECT_EQ(style.fill.a, 255);
    }
    EXPECT_EQ(StyleFor({.dark = false, .transparency = false}).fill, (Color{255, 243, 243, 243}));
}

TEST(Appearance, UsesTheHighContrastColors)
{
    const SystemAppearance appearance{
        .highContrast = true,
        .highContrastWindow = {255, 255, 255, 255},
        .highContrastText = {255, 0, 0, 0},
        .highContrastHighlight = {255, 0, 0, 255},
    };
    const auto style = StyleFor(appearance);
    EXPECT_FALSE(style.backdrop);
    EXPECT_EQ(style.fill, appearance.highContrastWindow);
    EXPECT_EQ(style.border, appearance.highContrastText);
    EXPECT_EQ(style.hover, appearance.highContrastHighlight);
    EXPECT_EQ(style.placeholder, appearance.highContrastText);
}

} // namespace
} // namespace harbor::island
