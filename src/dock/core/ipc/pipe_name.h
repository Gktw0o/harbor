#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace harbor::ipc
{

// Channel carrying control messages (ping/pong) between Harbor.Shell and the dock.
inline constexpr std::string_view kControlChannel = "control";

// Pipe name for one channel in one Windows session, e.g. "harbor.control.3" (without the
// "\\.\pipe\" prefix). Must stay identical to Harbor.Core's PipeNames; both sides test the same cases.
std::string PipeName(std::string_view channel, std::uint32_t sessionId);

// True for names the dock may connect to: "harbor." followed by 1-200 characters from [a-z0-9.-].
// Guards the --pipe-name override used by tests.
bool IsValidPipeName(std::string_view name) noexcept;

} // namespace harbor::ipc
