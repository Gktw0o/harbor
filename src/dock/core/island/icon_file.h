#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace harbor::island
{

// An icon file's pixels (contracts/README.md, "Icon files"): premultiplied BGRA, rows top to bottom, no
// padding. A view into the file's bytes, which the caller keeps alive.
struct IconView
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::uint8_t> pixels;
};

inline constexpr std::uint32_t kMaxIconSide = 256;
// The largest valid file: the header and 256 x 256 pixels.
inline constexpr std::size_t kMaxIconFileBytes = 12 + std::size_t{kMaxIconSide} * kMaxIconSide * 4;

// Checks a whole icon file: magic "HIC1", width and height 1-256, exactly that many pixels, colors
// premultiplied (none above its alpha). Anything else is no icon.
std::optional<IconView> ParseIconFile(std::span<const std::uint8_t> file) noexcept;

// The icon scaled down to fit a side x side square (centred, transparent around it), as premultiplied BGRA
// rows top to bottom: each pixel the average of the source pixels it covers. For the overflow menu's small
// icons. Empty for side 0.
std::vector<std::uint8_t> Downscale(const IconView& icon, std::uint32_t side);

} // namespace harbor::island
