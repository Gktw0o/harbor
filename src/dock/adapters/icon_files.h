#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace harbor::adapters
{

// Reads an icon file the Shell left in iconsDir. name comes from dockItems, which only allows plain file
// names. Empty if the file is missing, unreadable or larger than any valid icon file; core/island/icon_file
// checks the rest.
std::vector<std::uint8_t> ReadIconFile(const std::filesystem::path& iconsDir, std::string_view name);

} // namespace harbor::adapters
