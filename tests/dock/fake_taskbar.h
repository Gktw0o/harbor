#pragma once

#include <windows.h>

#include <chrono>
#include <combaseapi.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "adapters/win32_taskbar.h"

namespace harbor::test
{

// Stand-in taskbar windows for TaskbarTarget::Test, so tests never touch the real taskbar. They belong to
// a thread of the test process that pumps messages, because ShowWindowAsync (from this or any other
// process) is carried out by the window's own thread. They are 1x1 and fully transparent: nothing shows
// on screen, yet they count as visible.
class FakeTaskbar
{
  public:
    struct Layout
    {
        int secondaries = 1;
        // Secondary taskbars placed outside every monitor, like one whose display was unplugged.
        int offscreenSecondaries = 0;
    };

    FakeTaskbar(std::filesystem::path dataDir, Layout layout) : m_dataDir(std::move(dataDir))
    {
        GUID guid{};
        CoCreateGuid(&guid);
        wchar_t text[39]{};
        StringFromGUID2(guid, text, 39);
        for (const wchar_t c : std::wstring_view(text))
        {
            if (iswxdigit(c))
            {
                m_id.push_back(static_cast<wchar_t>(towlower(c)));
            }
        }
        m_primaryClass = L"Harbor.Test." + m_id + L".TrayWnd";
        m_secondaryClass = L"Harbor.Test." + m_id + L".SecondaryTrayWnd";

        std::promise<void> ready;
        auto created = ready.get_future();
        m_thread = std::thread([this, layout, &ready] { Run(layout, ready); });
        created.get();
    }

    explicit FakeTaskbar(std::filesystem::path dataDir) : FakeTaskbar(std::move(dataDir), Layout{})
    {
    }

    ~FakeTaskbar()
    {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        m_thread.join();
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        UnregisterClassW(m_primaryClass.c_str(), instance);
        UnregisterClassW(m_secondaryClass.c_str(), instance);
    }

    FakeTaskbar(const FakeTaskbar&) = delete;
    FakeTaskbar& operator=(const FakeTaskbar&) = delete;

    const std::wstring& Id() const noexcept
    {
        return m_id;
    }
    adapters::TaskbarTarget Target() const
    {
        return adapters::TaskbarTarget::Test(m_id, m_dataDir);
    }
    HWND Primary() const noexcept
    {
        return m_primary;
    }
    const std::vector<HWND>& Secondaries() const noexcept
    {
        return m_secondaries;
    }
    const std::vector<HWND>& OffscreenSecondaries() const noexcept
    {
        return m_offscreen;
    }

    // Hides a window the way Harbor will: from another thread, without waiting.
    static void Hide(HWND window)
    {
        ShowWindow(window, SW_HIDE);
    }

    // ShowWindowAsync completes on the window's thread, so poll.
    static bool WaitUntilVisible(HWND window, bool visible,
                                 std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while ((IsWindowVisible(window) != FALSE) != visible)
        {
            if (std::chrono::steady_clock::now() > deadline)
            {
                return false;
            }
            Sleep(10);
        }
        return true;
    }

    // The test target's stand-in for the auto-hide setting.
    std::filesystem::path AutoHideFile() const
    {
        return Target().TestAutoHideFile();
    }
    void SetAutoHide(std::uint32_t state) const
    {
        std::ofstream(AutoHideFile(), std::ios::binary | std::ios::trunc) << state;
    }
    std::uint32_t AutoHide() const
    {
        std::uint32_t state = 0;
        std::ifstream(AutoHideFile()) >> state;
        return state;
    }

  private:
    void Run(Layout layout, std::promise<void>& ready)
    {
        m_threadId = GetCurrentThreadId();
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        for (const auto& windowClass : {m_primaryClass, m_secondaryClass})
        {
            WNDCLASSEXW description{};
            description.cbSize = sizeof(description);
            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = instance;
            description.lpszClassName = windowClass.c_str();
            RegisterClassExW(&description);
        }

        m_primary = CreateFake(m_primaryClass, 0);
        for (int i = 0; i < layout.secondaries; ++i)
        {
            m_secondaries.push_back(CreateFake(m_secondaryClass, 0));
        }
        for (int i = 0; i < layout.offscreenSecondaries; ++i)
        {
            m_offscreen.push_back(CreateFake(m_secondaryClass, -30000));
        }
        ready.set_value();

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            DispatchMessageW(&message);
        }
        DestroyWindow(m_primary);
        for (const auto* list : {&m_secondaries, &m_offscreen})
        {
            for (const HWND window : *list)
            {
                DestroyWindow(window);
            }
        }
    }

    static HWND CreateFake(const std::wstring& windowClass, int position)
    {
        const HWND window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT, windowClass.c_str(), L"",
            WS_POPUP, position, position, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
        ShowWindow(window, SW_SHOWNA);
        return window;
    }

    std::filesystem::path m_dataDir;
    std::wstring m_id;
    std::wstring m_primaryClass;
    std::wstring m_secondaryClass;
    std::thread m_thread;
    DWORD m_threadId = 0;
    HWND m_primary = nullptr;
    std::vector<HWND> m_secondaries;
    std::vector<HWND> m_offscreen;
};

} // namespace harbor::test
