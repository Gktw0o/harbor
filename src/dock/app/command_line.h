#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace harbor::dock
{

// Options from harbor-dock.exe's command line. They exist for tests and development: a normal start
// passes none.
struct DockOptions
{
    // --pipe-name <name>: connect to this control pipe instead of harbor.control.<sessionId>.
    std::optional<std::string> pipeName;
    // --data-dir <absolute path>: keep logs and state here instead of %LOCALAPPDATA%\Harbor.
    std::optional<std::filesystem::path> dataDir;
};

// Parses the arguments that follow the program name. Anything unknown, repeated, missing its value or
// invalid is an error, returned as a message for the log.
std::variant<DockOptions, std::string> ParseCommandLine(std::span<const std::wstring_view> args);

} // namespace harbor::dock
