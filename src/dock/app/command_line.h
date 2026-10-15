#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace harbor::dock
{

// Options from harbor-dock.exe's command line. They exist for tests: a normal start passes none, and the
// three below are only accepted together, so a test dock never touches the real taskbar.
struct DockOptions
{
    // --pipe-name <name>: connect to this control pipe instead of harbor.control.<sessionId>.
    std::optional<std::string> pipeName;
    // --data-dir <absolute path>: keep logs and state here instead of %LOCALAPPDATA%\Harbor.
    std::optional<std::filesystem::path> dataDir;
    // --test-taskbar <id>: treat the test windows "Harbor.Test.<id>.*" as the taskbar (see
    // adapters/win32_taskbar.h). <id> is 1-64 characters from [a-z0-9-].
    std::optional<std::wstring> testTaskbarId;
    // --test-ping-timeout-ms <100-60000>: how long without a ping before the dock restores the taskbar
    // (normally 5 s), so tests don't wait. Only together with the three above.
    std::optional<std::uint32_t> testPingTimeoutMs;
    // --test-orphan-timeout-ms <100-600000>: how long a test dock waits without a control connection before
    // it exits (default 30 s; a normal dock waits forever). Only together with the three above.
    std::optional<std::uint32_t> testOrphanTimeoutMs;
};

// Parses the arguments that follow the program name. Anything unknown, repeated, missing its value or
// invalid is an error, returned as a message for the log.
std::variant<DockOptions, std::string> ParseCommandLine(std::span<const std::wstring_view> args);

} // namespace harbor::dock
