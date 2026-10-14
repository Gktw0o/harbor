// harbor-dock entry point and composition root.

#include <windows.h>

#include "app/dock_window.h"

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int showCommand)
{
    harbor::dock::DockWindow window;
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
