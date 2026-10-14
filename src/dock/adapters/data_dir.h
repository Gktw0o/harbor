#pragma once

#include <filesystem>

namespace harbor::adapters
{

// %LOCALAPPDATA%\Harbor, where Harbor keeps its logs and state. Empty if Windows can't say where that is.
std::filesystem::path DefaultDataDir() noexcept;

} // namespace harbor::adapters
