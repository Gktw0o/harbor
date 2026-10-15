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

} // namespace harbor::island
