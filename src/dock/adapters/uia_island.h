#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace harbor::adapters
{

// What UI Automation needs from the island. Called on the island's (UI) thread only.
class IIslandAccess
{
  public:
    virtual ~IIslandAccess() = default;

    virtual std::size_t ItemCount() const = 0;
    // Stays the same for an item while it stays on the dock, wherever it moves, so a screen reader's place
    // survives list changes. Never 0.
    virtual std::uint64_t ItemKey(std::size_t index) const = 0;
    // Where the item with this key is now; nullopt once it's gone.
    virtual std::optional<std::size_t> IndexOfKey(std::uint64_t key) const = 0;
    virtual std::wstring ItemName(std::size_t index) const = 0;
    // UI Automation's ItemStatus: "running, 2 windows", "needs attention"... Empty: nothing to say.
    virtual std::wstring ItemStatus(std::size_t index) const = 0;
    // Screen coordinates, physical pixels.
    virtual RECT ItemBounds(std::size_t index) const = 0;
    virtual std::optional<std::size_t> ItemAt(POINT screenPoint) const = 0;
    // Starts the item's app, as a click would. False if it can't.
    virtual bool InvokeItem(std::size_t index) = 0;
};

// The island for screen readers and other UI Automation clients: a tool bar whose items are buttons with the
// apps' names and an Invoke pattern (docs/adr/0009). Providers live on the UI thread (UIA marshals calls to
// it) and hold only an item's key and shared state that Disconnect empties: a client holding one for an item
// that is gone, or after the island went away, gets UIA_E_ELEMENTNOTAVAILABLE instead of a crash, and one
// for an item that only moved keeps working.
class IslandAccessibility
{
  public:
    IslandAccessibility(HWND window, IIslandAccess& island);
    // Disconnect().
    ~IslandAccessibility();
    IslandAccessibility(const IslandAccessibility&) = delete;
    IslandAccessibility& operator=(const IslandAccessibility&) = delete;

    // WM_GETOBJECT: the root provider for UiaRootObjectId; nullopt for anything else (DefWindowProc then).
    std::optional<LRESULT> OnGetObject(WPARAM wParam, LPARAM lParam);

    // The items changed: providers for items that are gone go dead, and listening clients hear the children
    // changed.
    void ItemsChanged();

    // The window is going: every provider goes dead and UI Automation lets go of them.
    void Disconnect() noexcept;

  private:
    struct Shared;
    std::shared_ptr<Shared> m_shared;
};

} // namespace harbor::adapters
