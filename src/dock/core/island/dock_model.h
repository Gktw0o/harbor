#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/ipc/messages.h"
#include "core/island/layout.h"
#include "core/windows/app_windows.h"

namespace harbor::island
{

// A pinned item, as Harbor Shell names it in appInfo.
struct PinRef
{
    ipc::DockItemKind kind = ipc::DockItemKind::App;
    std::string target;
    friend bool operator==(const PinRef&, const PinRef&) = default;
};

// What Harbor Shell answered about a running app (appInfo).
struct AppInfo
{
    std::string name;
    std::optional<std::string> icon;
    // The pin this app is, if the Shell matched it to one (a shortcut's target, say).
    std::optional<PinRef> pinned;
    friend bool operator==(const AppInfo&, const AppInfo&) = default;
};

// A running app, as the dock model needs it.
struct RunningApp
{
    windows::AppId id = 0;
    windows::AppIdentity identity;
    // Its name until the Shell answers (FallbackName).
    std::string fallbackName;
    // One of its windows asked for attention.
    bool attention = false;
    std::optional<AppInfo> info;
    // When the dock first saw it, on any steady clock.
    std::chrono::milliseconds seen{0};
};

// How long an app the Shell hasn't answered for stays off the dock (so it doesn't appear with a placeholder
// and then change), before it appears under its fallback name.
inline constexpr std::chrono::milliseconds kLookupWait{250};

// An app's name before Harbor Shell answers: its executable's file name without ".exe", else its
// AppUserModelID, else "App".
std::string FallbackName(std::string_view aumid, std::string_view exe);

// Whether the running app is that pin (docs/adr/0010): the Shell said so, or the pin's target is the app's
// AppUserModelID or, for an app without one, its executable. Without case.
bool IsPinnedAs(const ipc::DockItem& pin, const RunningApp& app);

struct DockSlot
{
    enum class Kind
    {
        Pin,
        App,
    };
    Kind kind = Kind::Pin;
    // Kind::Pin: its place in the pinned list.
    std::size_t pin = 0;
    // The running apps it stands for: any number for a pin, one for an app that isn't pinned.
    std::vector<windows::AppId> apps;
    std::string name;
    std::optional<std::string> icon;
    ItemMarks marks;
    friend bool operator==(const DockSlot&, const DockSlot&) = default;
};

struct DockModel
{
    // The pinned items in the user's order, then the running apps that aren't pinned, in the order given.
    std::vector<DockSlot> slots;
    // How many of the slots are pinned items: a divider goes after them when running apps follow.
    std::size_t pinned = 0;
    // An app is waiting for the Shell: build the model again at this time.
    std::optional<std::chrono::milliseconds> rebuildAt;
    friend bool operator==(const DockModel&, const DockModel&) = default;
};

// apps in the order unpinned ones are shown (AppWindows: the order they started). A running app joins the
// first pin it is; one the Shell hasn't answered for waits kLookupWait from when it was seen.
DockModel BuildDockModel(std::span<const ipc::DockItem> pins, std::span<const RunningApp> apps,
                         std::chrono::milliseconds now);

} // namespace harbor::island
