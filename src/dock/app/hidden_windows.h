#pragma once

#include <windows.h>

#include <array>
#include <atomic>

namespace harbor::dock
{

// The taskbar windows the dock has hidden, kept where a crash handler can find them: a fixed array of
// atomics, so reading it takes no lock and allocates nothing.
class HiddenWindows
{
  public:
    static constexpr size_t kCapacity = 16;

    // False if the list is full; that window is then left to Harbor Shell's watchdog and the next restore.
    bool Add(HWND window) noexcept
    {
        for (auto& slot : m_windows)
        {
            HWND expected = nullptr;
            if (slot.load() == window || slot.compare_exchange_strong(expected, window))
            {
                return true;
            }
        }
        return false;
    }

    void Clear() noexcept
    {
        for (auto& slot : m_windows)
        {
            slot.store(nullptr);
        }
    }

    template <typename Visit> void ForEach(Visit&& visit) const noexcept
    {
        for (const auto& slot : m_windows)
        {
            if (const HWND window = slot.load())
            {
                visit(window);
            }
        }
    }

  private:
    std::array<std::atomic<HWND>, kCapacity> m_windows{};
};

} // namespace harbor::dock
