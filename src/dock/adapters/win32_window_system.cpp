#include "adapters/win32_window_system.h"

// The shell's property store headers need OLE.
#include <ole2.h>

#include <algorithm>
#include <appmodel.h>
#include <array>
#include <dwmapi.h>
#include <propsys.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <wil/com.h>

namespace harbor::adapters
{
namespace
{

// PKEY_AppUserModel_ID ({9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5), spelled out so no import library is
// needed for it.
constexpr PROPERTYKEY kAppUserModelId{
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

// The hooks call back on the thread that set them, which is the one this object is used on.
thread_local Win32WindowSystem* t_system = nullptr;

HWND ToHwnd(windows::WindowId id) noexcept
{
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(id));
}

windows::WindowId ToId(HWND hwnd) noexcept
{
    return static_cast<windows::WindowId>(reinterpret_cast<std::uintptr_t>(hwnd));
}

std::string Utf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr,
                        nullptr);
    return utf8;
}

std::wstring ClassName(HWND window)
{
    std::array<wchar_t, 256> name{};
    const int length = GetClassNameW(window, name.data(), static_cast<int>(name.size()));
    return std::wstring(name.data(), static_cast<std::size_t>(std::max(length, 0)));
}

// Window class names compared in core are ASCII; anything else can't match them.
std::string Ascii(std::wstring_view text)
{
    std::string ascii;
    ascii.reserve(text.size());
    for (const wchar_t c : text)
    {
        ascii.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return ascii;
}

// The AppUserModelID the window itself carries, if its app set one.
std::wstring WindowAumid(HWND window)
{
    wil::com_ptr_nothrow<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))) || !store)
    {
        return {};
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring aumid;
    if (SUCCEEDED(store->GetValue(kAppUserModelId, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal != nullptr)
    {
        aumid = value.pwszVal;
    }
    PropVariantClear(&value);
    return aumid;
}

// A packaged process's AppUserModelID.
std::wstring PackageAumid(HANDLE process)
{
    UINT32 length = 0;
    if (GetApplicationUserModelId(process, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
    {
        return {};
    }
    std::wstring aumid(length, L'\0');
    if (GetApplicationUserModelId(process, &length, aumid.data()) != ERROR_SUCCESS)
    {
        return {};
    }
    aumid.resize(wcsnlen(aumid.c_str(), aumid.size()));
    return aumid;
}

std::wstring ImagePath(HANDLE process)
{
    std::wstring path(32768, L'\0');
    auto length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length))
    {
        return {};
    }
    path.resize(length);
    return path;
}

DWORD ProcessOf(HWND window) noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId;
}

// A packaged app's frame (ApplicationFrameWindow) belongs to ApplicationFrameHost; the app runs in the
// process of the CoreWindow the frame hosts.
DWORD HostedAppProcess(HWND frame, DWORD frameProcess)
{
    struct Search
    {
        DWORD frameProcess;
        DWORD found;
    } search{frameProcess, 0};
    EnumChildWindows(
        frame,
        [](HWND child, LPARAM context) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(context);
            const DWORD process = ProcessOf(child);
            if (process != search.frameProcess && ClassName(child) == L"Windows.UI.Core.CoreWindow")
            {
                search.found = process;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

} // namespace

Win32WindowSystem::~Win32WindowSystem()
{
    Stop();
}

bool Win32WindowSystem::Start(std::function<void()> wake)
{
    Stop();
    m_wake = std::move(wake);
    t_system = this;
    // Out of context (no code in other processes), not for the dock's own windows.
    constexpr std::pair<DWORD, DWORD> kRanges[] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    for (const auto& [first, last] : kRanges)
    {
        wil::unique_hwineventhook hook(SetWinEventHook(first, last, nullptr, &Win32WindowSystem::OnEvent, 0,
                                                       0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        if (!hook)
        {
            Stop();
            return false;
        }
        m_hooks.push_back(std::move(hook));
    }
    // The shell hook, for flashes (attention): documented, but "not intended for general use" (ADR 0011).
    // Without it, everything but attention marks still works.
    WNDCLASSEXW shellHookClass{};
    shellHookClass.cbSize = sizeof(shellHookClass);
    shellHookClass.lpfnWndProc = &Win32WindowSystem::ShellHookProc;
    shellHookClass.hInstance = GetModuleHandleW(nullptr);
    shellHookClass.lpszClassName = L"Harbor.Dock.ShellHook";
    RegisterClassExW(&shellHookClass);
    m_shellHookMessage = RegisterWindowMessageW(L"SHELLHOOK");
    // A hidden top-level window: shell hook messages go to top-level windows.
    m_shellHookWindow = CreateWindowExW(WS_EX_TOOLWINDOW, shellHookClass.lpszClassName, L"", WS_POPUP, 0, 0,
                                        0, 0, nullptr, nullptr, shellHookClass.hInstance, nullptr);
    if (m_shellHookWindow != nullptr)
    {
        RegisterShellHookWindow(m_shellHookWindow);
    }
    return true;
}

void Win32WindowSystem::Stop() noexcept
{
    if (m_shellHookWindow != nullptr)
    {
        DeregisterShellHookWindow(m_shellHookWindow);
        DestroyWindow(m_shellHookWindow);
        m_shellHookWindow = nullptr;
    }
    // On the thread that set them, as UnhookWinEvent requires.
    m_hooks.clear();
    if (t_system == this)
    {
        t_system = nullptr;
    }
}

void CALLBACK Win32WindowSystem::OnEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG child,
                                         DWORD, DWORD)
{
    auto* self = t_system;
    if (self == nullptr || hwnd == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF)
    {
        return;
    }
    using Kind = windows::WindowEvent::Kind;
    Kind kind = Kind::Changed;
    switch (event)
    {
    case EVENT_OBJECT_DESTROY:
        kind = Kind::Destroyed;
        break;
    case EVENT_SYSTEM_FOREGROUND:
        kind = Kind::Foreground;
        break;
    case EVENT_SYSTEM_MOVESIZESTART:
        kind = Kind::MoveSizeStart;
        break;
    case EVENT_SYSTEM_MOVESIZEEND:
        kind = Kind::MoveSizeEnd;
        break;
    default:
        break;
    }
    // Only top-level windows; a destroyed one can't be asked any more (unknown ones are ignored later).
    if (kind != Kind::Destroyed && GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow())
    {
        return;
    }
    // Only queue: these callbacks also come while a COM call pumps messages, in the middle of other work.
    self->Queue({kind, ToId(hwnd)});
}

void Win32WindowSystem::Queue(windows::WindowEvent event)
{
    if (m_events.size() >= kMaxQueued)
    {
        m_overflowed = true;
    }
    else
    {
        m_events.push_back(event);
    }
    if (m_wake)
    {
        m_wake();
    }
}

std::vector<windows::WindowEvent> Win32WindowSystem::TakeEvents(bool& overflowed)
{
    overflowed = std::exchange(m_overflowed, false);
    return std::exchange(m_events, {});
}

std::vector<windows::WindowId> Win32WindowSystem::TopLevelWindows()
{
    std::vector<windows::WindowId> windows;
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            try
            {
                reinterpret_cast<std::vector<windows::WindowId>*>(context)->push_back(ToId(window));
                return TRUE;
            }
            catch (...)
            {
                return FALSE;
            }
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

std::optional<windows::WindowFacts> Win32WindowSystem::FactsOf(windows::WindowId id)
{
    const HWND window = ToHwnd(id);
    if (!IsWindow(window))
    {
        return std::nullopt;
    }
    windows::WindowFacts facts;
    facts.visible = IsWindowVisible(window) != FALSE;
    facts.owned = GetWindow(window, GW_OWNER) != nullptr;
    const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    facts.toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
    facts.appWindow = (exStyle & WS_EX_APPWINDOW) != 0;
    facts.noActivate = (exStyle & WS_EX_NOACTIVATE) != 0;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
    {
        using Cloak = windows::Cloak;
        facts.cloak = (cloaked & DWM_CLOAKED_SHELL) != 0       ? Cloak::Shell
                      : (cloaked & DWM_CLOAKED_INHERITED) != 0 ? Cloak::Inherited
                      : (cloaked & DWM_CLOAKED_APP) != 0       ? Cloak::App
                                                               : Cloak::None;
    }
    if (RECT rect{}; GetWindowRect(window, &rect))
    {
        facts.rect = {rect.left, rect.top, rect.right, rect.bottom};
    }
    facts.className = Ascii(ClassName(window));
    const DWORD process = ProcessOf(window);
    facts.harbor = process == GetCurrentProcessId();
    facts.explorer = process != 0 && process == ProcessOf(GetShellWindow());
    return facts;
}

std::optional<windows::WindowApp> Win32WindowSystem::AppOf(windows::WindowId id)
{
    const HWND window = ToHwnd(id);
    DWORD process = ProcessOf(window);
    if (process == 0)
    {
        return std::nullopt;
    }
    if (ClassName(window) == L"ApplicationFrameWindow")
    {
        if (const DWORD hosted = HostedAppProcess(window, process); hosted != 0)
        {
            process = hosted;
        }
    }
    windows::WindowApp app{.aumid = Utf8(WindowAumid(window)), .processId = process};
    if (const auto facts = FactsOfProcess(process))
    {
        app.exe = facts->exe;
        app.processStarted = facts->started;
        if (app.aumid.empty())
        {
            app.aumid = facts->packageAumid;
        }
    }
    return app;
}

bool Win32WindowSystem::IsMinimized(windows::WindowId window)
{
    return IsIconic(ToHwnd(window)) != FALSE;
}

bool Win32WindowSystem::IsHung(windows::WindowId window)
{
    // Looks at how long the window's thread hasn't taken messages; sends it none.
    return IsHungAppWindow(ToHwnd(window)) != FALSE;
}

LRESULT CALLBACK Win32WindowSystem::ShellHookProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = t_system;
    // HSHELL_FLASH (HSHELL_REDRAW with the high bit): the window flashed its taskbar button (docs/adr/0011).
    if (self != nullptr && message == self->m_shellHookMessage && message != 0 && wParam == HSHELL_FLASH &&
        lParam != 0)
    {
        self->Queue({windows::WindowEvent::Kind::Flash, ToId(reinterpret_cast<HWND>(lParam))});
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

windows::WindowId Win32WindowSystem::RootOwner(windows::WindowId window)
{
    const HWND root = GetAncestor(ToHwnd(window), GA_ROOTOWNER);
    return root != nullptr ? ToId(root) : window;
}

std::optional<Win32WindowSystem::ProcessFacts> Win32WindowSystem::FactsOfProcess(DWORD processId)
{
    const wil::unique_process_handle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process)
    {
        return std::nullopt;
    }
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(process.get(), &created, &exited, &kernel, &user))
    {
        return std::nullopt;
    }
    const std::uint64_t started =
        static_cast<std::uint64_t>(created.dwHighDateTime) << 32 | created.dwLowDateTime;
    if (const auto cached = m_processes.find(processId);
        cached != m_processes.end() && cached->second.started == started)
    {
        return cached->second;
    }
    ProcessFacts facts{.started = started,
                       .exe = Utf8(ImagePath(process.get())),
                       .packageAumid = Utf8(PackageAumid(process.get()))};
    // Bounded: process ids come and go.
    if (m_processes.size() >= 512)
    {
        m_processes.clear();
    }
    m_processes[processId] = facts;
    return facts;
}

} // namespace harbor::adapters
