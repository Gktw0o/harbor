// A windowless GUI app for launch tests: when started, it writes "<its own path>.launched" (holding its
// command line, UTF-16) and exits. Tests pin a copy of it and check the file appears once the dock launched
// it.

#include <windows.h>

#include <string>

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size())
    {
        return 1;
    }
    path.resize(length);
    path += L".launched";

    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return 2;
    }
    const std::wstring commandLine = GetCommandLineW();
    DWORD written = 0;
    WriteFile(file, commandLine.data(), static_cast<DWORD>(commandLine.size() * sizeof(wchar_t)), &written,
              nullptr);
    CloseHandle(file);
    return 0;
}
