#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

enum class DockItemKind
{
    // The target is an AppUserModelID, launched as shell:AppsFolder\<target>.
    App,
    // The target is an absolute path to an .exe or .lnk.
    Path,
};

// One app on the dock. Text is UTF-8.
struct DockItem
{
    DockItemKind kind = DockItemKind::App;
    std::string target;
    // Display name, for people and screen readers.
    std::string name;
    // File name of the item's icon in the icons folder (contracts/README.md), if the Shell could make one.
    std::optional<std::string> icon;
    friend bool operator==(const DockItem&, const DockItem&) = default;
};

// Harbor.Shell to dock: the apps the dock shows, in order (contracts/dockItems.schema.json, v1). Sent on
// every connect and whenever the list changes.
struct DockItems
{
    // Bounds that keep the largest message under the line limit. Lengths count Unicode characters.
    static constexpr std::size_t kMaxItems = 40;
    static constexpr std::size_t kMaxTargetLength = 260;
    static constexpr std::size_t kMaxNameLength = 128;

    std::vector<DockItem> items;
    friend bool operator==(const DockItems&, const DockItems&) = default;
};

// Something the dock can start or pin, as in dockItems (bounded like DockItem::target).
struct AppTarget
{
    DockItemKind kind = DockItemKind::App;
    std::string target;
    friend bool operator==(const AppTarget&, const AppTarget&) = default;
};

// Dock to Harbor.Shell: a running app the dock hasn't had named yet (contracts/appLookup.schema.json, v1).
// The Shell answers with AppInfo for the same id.
struct AppLookup
{
    static constexpr std::size_t kMaxAumidLength = 130;
    static constexpr std::size_t kMaxExeLength = 2048;

    // The dock's number for the app, never reused for another app. Not 0.
    std::uint32_t id = 0;
    std::optional<std::string> aumid;
    std::optional<std::string> exe;
    // One of its windows (HWND), for the Shell to read the window's relaunch name and icon.
    std::optional<std::uint32_t> window;
    friend bool operator==(const AppLookup&, const AppLookup&) = default;
};

// Harbor.Shell to dock: what the Shell knows about a running app (contracts/appInfo.schema.json, v1). May
// come twice for one id: at once from what the Shell knows, then with the real name and icon.
struct AppInfo
{
    std::uint32_t id = 0;
    // Bounded like DockItem::name.
    std::string name;
    std::optional<std::string> icon;
    // The pinned item this app is.
    std::optional<AppTarget> pinned;
    // How to start it again, and what pinning it pins.
    std::optional<AppTarget> launch;
    friend bool operator==(const AppInfo&, const AppInfo&) = default;
};

enum class PinAction
{
    Pin,
    Unpin,
};

// Dock to Harbor.Shell: the user asked to pin or unpin an app (contracts/pinRequest.schema.json, v1). The
// dock changes nothing until the Shell's next dockItems.
struct PinRequest
{
    PinAction action = PinAction::Pin;
    AppTarget item;
    friend bool operator==(const PinRequest&, const PinRequest&) = default;
};

using Message = std::variant<Ping, Pong, SetTaskbar, TaskbarState, DockItems, AppLookup, AppInfo, PinRequest>;

} // namespace harbor::ipc
