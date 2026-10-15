// App windows for the dock's end-to-end tests (docs/adr/0010). A test dock (--test-taskbar <id>) follows only
// windows of the class "Harbor.Test.<id>.App"; these are such windows, off screen, so tests never see the
// user's windows and the user doesn't see these.
//
// harbor_test_windows <id>
//     Reads commands from standard input, one a line, and answers each with a line: "ok" (with a number for
//     new and activate) or "error <what>". End of input quits.
//
//     new <n> [<aumid>]  window n, carrying that AppUserModelID if given, shown without activating it
//     aumid <n> <aumid>  sets its AppUserModelID later ("-" clears it)
//     show <n>, hide <n>, minimize <n>, restore <n>, cloak <n>, uncloak <n>, flash <n>, close <n>
//     activate <n>       SetForegroundWindow; answers "ok 1" if it took
//     hang <ms>          the windows' thread sleeps (a hung app)
//     quit

#include <windows.h>

#include <ole2.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dwmapi.h>
#include <iostream>
#include <map>
#include <memory>
#include <propsys.h>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>

namespace
{

// PKEY_AppUserModel_ID.
constexpr PROPERTYKEY kAppUserModelId{
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
constexpr UINT kCommandMessage = WM_APP + 1;

std::wstring Wide(const std::string& text)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

void Answer(const std::string& line)
{
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

bool SetAumid(HWND window, const std::string& aumid)
{
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store))) || store == nullptr)
    {
        return false;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (aumid != "-")
    {
        const auto wide = Wide(aumid);
        const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
        value.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (value.pwszVal == nullptr)
        {
            store->Release();
            return false;
        }
        std::memcpy(value.pwszVal, wide.c_str(), bytes);
        value.vt = VT_LPWSTR;
    }
    const HRESULT result = store->SetValue(kAppUserModelId, value);
    PropVariantClear(&value);
    store->Release();
    return SUCCEEDED(result);
}

class Windows
{
  public:
    explicit Windows(std::wstring className) : m_className(std::move(className))
    {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = m_className.c_str();
        RegisterClassExW(&windowClass);
    }

    std::string Run(const std::string& line)
    {
        std::istringstream words(line);
        std::string command;
        int number = 0;
        words >> command;
        if (command == "quit")
        {
            PostQuitMessage(0);
            return "ok";
        }
        if (command == "hang")
        {
            words >> number;
            Sleep(static_cast<DWORD>(number));
            return "ok";
        }
        if (!(words >> number))
        {
            return "error bad command";
        }
        std::string argument;
        words >> argument;
        if (command == "new")
        {
            if (m_windows.contains(number))
            {
                return "error exists";
            }
            // Off screen, unowned, not a tool window: an app window the user doesn't see.
            const HWND window = CreateWindowExW(
                0, m_className.c_str(), (L"Test " + std::to_wstring(number)).c_str(), WS_OVERLAPPEDWINDOW,
                -20000, -20000, 200, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (window == nullptr)
            {
                return "error create";
            }
            if (!argument.empty() && !SetAumid(window, argument))
            {
                return "error aumid";
            }
            m_windows[number] = window;
            ShowWindow(window, SW_SHOWNOACTIVATE);
            return "ok " + std::to_string(reinterpret_cast<std::uintptr_t>(window));
        }
        const auto found = m_windows.find(number);
        if (found == m_windows.end())
        {
            return "error no window";
        }
        const HWND window = found->second;
        if (command == "aumid")
        {
            return SetAumid(window, argument) ? "ok" : "error aumid";
        }
        if (command == "show" || command == "hide" || command == "minimize" || command == "restore")
        {
            ShowWindow(window, command == "show"       ? SW_SHOWNOACTIVATE
                               : command == "hide"     ? SW_HIDE
                               : command == "minimize" ? SW_SHOWMINNOACTIVE
                                                       : SW_SHOWNOACTIVATE);
            return "ok";
        }
        if (command == "cloak" || command == "uncloak")
        {
            const BOOL cloak = command == "cloak";
            return SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_CLOAK, &cloak, sizeof(cloak)))
                       ? "ok"
                       : "error cloak";
        }
        if (command == "flash")
        {
            FLASHWINFO flash{sizeof(flash), window, FLASHW_TRAY, 3, 0};
            FlashWindowEx(&flash);
            return "ok";
        }
        if (command == "activate")
        {
            return SetForegroundWindow(window) ? "ok 1" : "ok 0";
        }
        if (command == "close")
        {
            DestroyWindow(window);
            m_windows.erase(found);
            return "ok";
        }
        return "error bad command";
    }

  private:
    std::wstring m_className;
    std::map<int, HWND> m_windows;
};

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2)
    {
        return 1;
    }
    // Shell property stores are COM.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        return 2;
    }
    Windows windows(std::wstring(L"Harbor.Test.") + argv[1] + L".App");

    // Make the message queue before the reader can post to it.
    MSG message{};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    const DWORD uiThread = GetCurrentThreadId();
    std::thread([uiThread] {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            PostThreadMessageW(uiThread, kCommandMessage, 0, reinterpret_cast<LPARAM>(new std::string(line)));
        }
        PostThreadMessageW(uiThread, kCommandMessage, 0, reinterpret_cast<LPARAM>(new std::string("quit")));
    }).detach();

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (message.hwnd == nullptr && message.message == kCommandMessage)
        {
            const std::unique_ptr<std::string> line(reinterpret_cast<std::string*>(message.lParam));
            Answer(windows.Run(*line));
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return 0;
}
