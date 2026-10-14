#include "core/ipc/pipe_name.h"

#include <algorithm>

namespace harbor::ipc
{

std::string PipeName(std::string_view channel, std::uint32_t sessionId)
{
    std::string name = "harbor.";
    name.append(channel);
    name.push_back('.');
    name.append(std::to_string(sessionId));
    return name;
}

bool IsValidPipeName(std::string_view name) noexcept
{
    constexpr std::string_view kPrefix = "harbor.";
    constexpr std::size_t kMaxSuffixLength = 200;
    if (!name.starts_with(kPrefix))
    {
        return false;
    }
    const auto suffix = name.substr(kPrefix.size());
    return !suffix.empty() && suffix.size() <= kMaxSuffixLength && std::ranges::all_of(suffix, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    });
}

} // namespace harbor::ipc
