#include "core/ipc/pipe_name.h"

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

} // namespace harbor::ipc
