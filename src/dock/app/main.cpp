// harbor-dock entry point and composition root.
//
// Usage: harbor-dock.exe [--pipe-name <name>]
//   --pipe-name  Connect to this control pipe instead of harbor.control.<sessionId> (used by tests).

#include <windows.h>

#include <optional>
#include <shellapi.h>
#include <string>
#include <wil/resource.h>

#include "adapters/named_pipe_client.h"
#include "app/dock_window.h"
#include "core/ipc/pipe_name.h"

namespace
{

// The control pipe name from --pipe-name, or the default for this session. Empty if the override is invalid.
std::optional<std::string> ControlPipeName()
{
    int argc = 0;
    const wil::unique_hlocal_ptr<PWSTR[]> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    if (argv)
    {
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::wstring_view(argv[i]) == L"--pipe-name")
            {
                const std::wstring_view value = argv[i + 1];
                std::string name;
                for (const wchar_t c : value)
                {
                    if (c > 0x7F)
                    {
                        return std::nullopt;
                    }
                    name.push_back(static_cast<char>(c));
                }
                return harbor::ipc::IsValidPipeName(name) ? std::optional(name) : std::nullopt;
            }
        }
    }

    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    return harbor::ipc::PipeName(harbor::ipc::kControlChannel, sessionId);
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand)
{
    const auto pipeName = ControlPipeName();
    if (!pipeName)
    {
        OutputDebugStringW(L"harbor-dock: --pipe-name must be \"harbor.\" followed by [a-z0-9.-]\n");
        return ERROR_INVALID_PARAMETER;
    }

    harbor::adapters::NamedPipeClient controlChannel(harbor::adapters::PipePath(*pipeName));
    harbor::dock::DockWindow window(controlChannel);
    if (!window.Create(instance, showCommand))
    {
        return static_cast<int>(GetLastError());
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
