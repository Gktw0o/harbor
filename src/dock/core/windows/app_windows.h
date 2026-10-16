#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace harbor::windows
{

using WindowId = std::uint64_t;
// Numbers the dock gives apps (appLookup's id): one per app identity for the dock's lifetime, never reused
// for another app. Never 0.
using AppId = std::uint32_t;

// How the dock tells apps apart (docs/adr/0010): the AppUserModelID if the window or its process has one,
// else the executable's path, else (a process the dock can't look into) the process itself.
struct AppIdentity
{
    enum class Kind
    {
        Aumid,
        Exe,
        Process,
    };
    Kind kind = Kind::Process;
    // ASCII letters lower-cased: Windows compares AppUserModelIDs and paths without case.
    std::string key;
    friend bool operator==(const AppIdentity&, const AppIdentity&) = default;
    friend auto operator<=>(const AppIdentity&, const AppIdentity&) = default;
};

// The identity of a window whose app has this AppUserModelID (empty: none) and executable path (empty:
// unknown), in process processId.
AppIdentity IdentityOf(std::string_view aumid, std::string_view exe, std::uint32_t processId);

// ASCII letters lower-cased, everything else as is.
std::string LowerAscii(std::string_view text);

// What the window watcher knows about an app window.
struct TrackedWindow
{
    WindowId id = 0;
    AppIdentity app;
    // What the identity came from, for Harbor Shell's lookup (UTF-8; empty: unknown).
    std::string aumid;
    std::string exe;
    // When its process started (FILETIME ticks): apps are ordered by it.
    std::uint64_t processStarted = 0;
    bool minimized = false;
    friend bool operator==(const TrackedWindow&, const TrackedWindow&) = default;
};

// The app windows the dock knows, grouped by app (docs/adr/0010). Pure: the window watcher feeds it.
// - Each window keeps the place it was first seen in: clicks cycle through windows in that order, which
//   doesn't change when one comes to the front.
// - Each window remembers when it was last in front; the watcher seeds that from the z-order at start by
//   reporting windows as activated from back to front.
// - Apps are ordered by when their first window's process started; that stays while the app has windows, so
//   items don't jump when a process exits.
class AppWindows
{
  public:
    struct Window
    {
        WindowId id = 0;
        // Order first seen in (1, 2, ...) and of the last activation (0: never in front).
        std::uint64_t seen = 0;
        std::uint64_t active = 0;
        bool minimized = false;
        // It flashed (asked for attention, docs/adr/0011) and its app hasn't come forward since.
        bool attention = false;
        friend bool operator==(const Window&, const Window&) = default;
    };

    struct App
    {
        AppId id = 0;
        AppIdentity identity;
        // From its first window, for the lookup.
        std::string aumid;
        std::string exe;
        std::uint64_t started = 0;
        // When its group was made (in the windows' "seen" sequence): breaks ties in the order.
        std::uint64_t created = 0;
        // In the order first seen.
        std::vector<Window> windows;
        friend bool operator==(const App&, const App&) = default;
    };

    // A window is (still) an app window: added, or updated. If its app changed (an AppUserModelID set late),
    // it moves to that app, keeping its place in the cycling order.
    void Upsert(const TrackedWindow& window);
    // No longer an app window (destroyed, hidden, cloaked, on another desktop). Unknown ids are ignored.
    void Remove(WindowId id);
    // A window came to the front: its app's windows no longer ask for attention. Unknown ids are ignored: the
    // foreground often isn't an app window.
    void Activated(WindowId id);
    // A window asks for attention (it flashed). Unknown ids are ignored.
    void Flash(WindowId id);

    // Every app with windows, in the order their processes started (first seen breaks ties).
    const std::vector<App>& Apps() const noexcept
    {
        return m_apps;
    }
    std::optional<AppId> AppOf(WindowId id) const;
    // Bumped by every change.
    std::uint64_t Revision() const noexcept
    {
        return m_revision;
    }

  private:
    App* FindApp(AppId id);
    AppId IdFor(const AppIdentity& identity);
    void RemoveFromApp(AppId app, WindowId id, Window* removed);
    void Sort();

    std::vector<App> m_apps;
    std::map<WindowId, AppId> m_appOfWindow;
    // Kept after an app's last window goes, so it gets the same id (and the dock keeps what the Shell said
    // about it) when it comes back. Process identities are forgotten: process ids are reused.
    std::map<AppIdentity, AppId> m_ids;
    AppId m_nextId = 1;
    std::uint64_t m_nextSeen = 1;
    std::uint64_t m_nextActive = 1;
    std::uint64_t m_revision = 0;
};

} // namespace harbor::windows
