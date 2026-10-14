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

// Harbor.Shell to dock: hide the taskbar, or stop hiding it (contracts/setTaskbar.schema.json, v1). Sent on
// every connect and whenever the user changes it.
struct SetTaskbar
{
    bool hidden = false;
    friend bool operator==(const SetTaskbar&, const SetTaskbar&) = default;
};

// Why the dock doesn't hide the taskbar although it was asked to (taskbarState's "blocked").
enum class TaskbarBlock
{
    None,
    // Another app holds the panic hotkey: hiding without a way out is not allowed.
    HotkeyUnavailable,
    // The user pressed the panic hotkey; cleared by the next setTaskbar {hidden: false}.
    PanicHotkey,
    // Explorer kept showing the taskbar again; cleared like PanicHotkey.
    ExplorerFighting,
    // There is no taskbar (e.g. Explorer isn't running).
    TaskbarNotFound,
    // The dock runs in a job that could end it without warning, leaving nothing to restore the taskbar.
    KillOnCloseJob,
};

// Dock to Harbor.Shell: whether the dock hides the taskbar now and, if it was asked to but doesn't, why
// (contracts/taskbarState.schema.json, v1). Sent whenever either changes and on every connect.
struct TaskbarState
{
    bool hidden = false;
    TaskbarBlock blocked = TaskbarBlock::None;
    friend bool operator==(const TaskbarState&, const TaskbarState&) = default;
};

using Message = std::variant<Ping, Pong, SetTaskbar, TaskbarState>;

} // namespace harbor::ipc
