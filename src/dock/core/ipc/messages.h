#pragma once

#include <cstdint>
#include <variant>

namespace harbor::ipc
{

// Liveness probe, Harbor.Shell to dock (contracts/ping.schema.json, v1).
struct Ping
{
    std::uint32_t seq = 0;
    friend bool operator==(const Ping&, const Ping&) = default;
};

// Answer to a Ping, dock to Harbor.Shell, echoing its seq (contracts/pong.schema.json, v1).
struct Pong
{
    std::uint32_t seq = 0;
    friend bool operator==(const Pong&, const Pong&) = default;
};

using Message = std::variant<Ping, Pong>;

} // namespace harbor::ipc
