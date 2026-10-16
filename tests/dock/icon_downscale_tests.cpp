#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "core/island/icon_file.h"

namespace harbor::island
{
namespace
{

// A width x height icon of one premultiplied BGRA color.
std::vector<std::uint8_t> Solid(std::uint32_t width, std::uint32_t height, std::uint8_t b, std::uint8_t g,
                                std::uint8_t r, std::uint8_t a)
{
    std::vector<std::uint8_t> pixels;
    for (std::uint32_t i = 0; i < width * height; ++i)
    {
        pixels.insert(pixels.end(), {b, g, r, a});
    }
    return pixels;
}

std::vector<std::uint8_t> PixelAt(const std::vector<std::uint8_t>& image, std::uint32_t side, std::uint32_t x,
                                  std::uint32_t y)
{
    const auto at = (static_cast<std::size_t>(y) * side + x) * 4;
    return {image.begin() + at, image.begin() + at + 4};
}

TEST(IconDownscale, AveragesTheSourcePixelsEachTargetPixelCovers)
{
    // 4 x 4: left half opaque red, right half transparent; down to 2 x 2.
    std::vector<std::uint8_t> pixels;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const std::uint8_t a = x < 2 ? 255 : 0;
            pixels.insert(pixels.end(), {0, 0, a, a});
        }
    }
    const auto small = Downscale({4, 4, pixels}, 2);
    ASSERT_EQ(small.size(), 2u * 2 * 4);
    EXPECT_EQ(PixelAt(small, 2, 0, 0), (std::vector<std::uint8_t>{0, 0, 255, 255}));
    EXPECT_EQ(PixelAt(small, 2, 1, 1), (std::vector<std::uint8_t>{0, 0, 0, 0}));

    // All of it into one pixel: half red, half clear, averaged (and rounded).
    const auto one = Downscale({4, 4, pixels}, 1);
    EXPECT_EQ(PixelAt(one, 1, 0, 0), (std::vector<std::uint8_t>{0, 0, 128, 128}));
}

TEST(IconDownscale, AWideIconIsCentredWithClearRowsAroundIt)
{
    const auto wide = Solid(8, 4, 10, 20, 30, 255);
    const auto out = Downscale({8, 4, wide}, 4);
    // 4 x 2 in the middle rows of a 4 x 4 square.
    EXPECT_EQ(PixelAt(out, 4, 0, 0), (std::vector<std::uint8_t>{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(out, 4, 0, 1), (std::vector<std::uint8_t>{10, 20, 30, 255}));
    EXPECT_EQ(PixelAt(out, 4, 3, 2), (std::vector<std::uint8_t>{10, 20, 30, 255}));
    EXPECT_EQ(PixelAt(out, 4, 3, 3), (std::vector<std::uint8_t>{0, 0, 0, 0}));
}

TEST(IconDownscale, ASmallIconKeepsItsSize)
{
    const auto tiny = Solid(2, 2, 1, 2, 3, 4);
    const auto out = Downscale({2, 2, tiny}, 4);
    EXPECT_EQ(PixelAt(out, 4, 0, 0), (std::vector<std::uint8_t>{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(out, 4, 1, 1), (std::vector<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_EQ(PixelAt(out, 4, 2, 2), (std::vector<std::uint8_t>{1, 2, 3, 4}));
    EXPECT_TRUE(Downscale({2, 2, tiny}, 0).empty());
}

} // namespace
} // namespace harbor::island
