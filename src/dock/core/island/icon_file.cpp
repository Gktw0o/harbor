#include "core/island/icon_file.h"

#include <algorithm>

namespace harbor::island
{
namespace
{

constexpr std::uint8_t kMagic[] = {'H', 'I', 'C', '1'};
constexpr std::size_t kHeaderBytes = 12;

std::uint32_t ReadUint32(std::span<const std::uint8_t> bytes) noexcept
{
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
           static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

} // namespace

std::optional<IconView> ParseIconFile(std::span<const std::uint8_t> file) noexcept
{
    if (file.size() < kHeaderBytes || !std::equal(std::begin(kMagic), std::end(kMagic), file.begin()))
    {
        return std::nullopt;
    }
    const std::uint32_t width = ReadUint32(file.subspan(4, 4));
    const std::uint32_t height = ReadUint32(file.subspan(8, 4));
    if (width == 0 || height == 0 || width > kMaxIconSide || height > kMaxIconSide)
    {
        return std::nullopt;
    }
    // At most 256 * 256 * 4: no overflow.
    const std::size_t pixelBytes = std::size_t{width} * height * 4;
    if (file.size() != kHeaderBytes + pixelBytes)
    {
        return std::nullopt;
    }
    const auto pixels = file.subspan(kHeaderBytes);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        const std::uint8_t alpha = pixels[i + 3];
        if (pixels[i] > alpha || pixels[i + 1] > alpha || pixels[i + 2] > alpha)
        {
            return std::nullopt;
        }
    }
    return IconView{width, height, pixels};
}

std::vector<std::uint8_t> Downscale(const IconView& icon, std::uint32_t side)
{
    std::vector<std::uint8_t> out(std::size_t{side} * side * 4, 0);
    if (side == 0 || icon.width == 0 || icon.height == 0)
    {
        return out;
    }
    // Fit the longer side; an icon already small enough keeps its size.
    const std::uint32_t longer = std::max(icon.width, icon.height);
    const std::uint32_t width =
        longer <= side ? icon.width : std::max<std::uint32_t>(1, icon.width * side / longer);
    const std::uint32_t height =
        longer <= side ? icon.height : std::max<std::uint32_t>(1, icon.height * side / longer);
    const std::uint32_t left = (side - width) / 2;
    const std::uint32_t top = (side - height) / 2;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        // The source rows and columns this pixel covers (at least one of each).
        const std::uint32_t fromY = y * icon.height / height;
        const std::uint32_t toY = std::max(fromY + 1, (y + 1) * icon.height / height);
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint32_t fromX = x * icon.width / width;
            const std::uint32_t toX = std::max(fromX + 1, (x + 1) * icon.width / width);
            std::uint32_t sum[4]{};
            for (std::uint32_t sy = fromY; sy < toY; ++sy)
            {
                for (std::uint32_t sx = fromX; sx < toX; ++sx)
                {
                    const auto* pixel = icon.pixels.data() + (std::size_t{sy} * icon.width + sx) * 4;
                    for (int c = 0; c < 4; ++c)
                    {
                        sum[c] += pixel[c];
                    }
                }
            }
            const std::uint32_t count = (toY - fromY) * (toX - fromX);
            auto* target = out.data() + (std::size_t{top + y} * side + left + x) * 4;
            for (int c = 0; c < 4; ++c)
            {
                // Rounded; averaging premultiplied pixels keeps them premultiplied.
                target[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
            }
        }
    }
    return out;
}

} // namespace harbor::island
