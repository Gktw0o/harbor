#include "adapters/app_launcher.h"

#include <windows.h>

#include <ole2.h>

#include <algorithm>
#include <exdisp.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <string_view>
#include <wil/com.h>
#include <wil/resource.h>

namespace harbor::adapters
{
namespace
{

std::wstring Widen(std::string_view utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), length);
    return wide;
}

// The desktop window answers within 2 s, so COM calls into Explorer won't hang on a hung Explorer.
bool ExplorerResponds() noexcept
{
    DWORD_PTR answer = 0;
    const HWND desktop = GetShellWindow();
    return desktop != nullptr &&
           SendMessageTimeoutW(desktop, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &answer) != 0;
}

// Raymond Chen's documented route to the desktop's IShellDispatch2: ShellWindows -> the desktop's browser ->
// its view -> the view's background object -> Application.
HRESULT LaunchThroughExplorer(const std::wstring& target) noexcept
{
    if (!ExplorerResponds())
    {
        return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    wil::com_ptr_nothrow<IShellWindows> windows;
    RETURN_IF_FAILED(
        CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows)));
    wil::unique_variant empty;
    long window = 0;
    wil::com_ptr_nothrow<IDispatch> desktop;
    RETURN_IF_FAILED(
        windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &window, SWFO_NEEDDISPATCH, &desktop));
    RETURN_HR_IF_NULL(E_FAIL, desktop);
    wil::com_ptr_nothrow<IServiceProvider> provider;
    RETURN_IF_FAILED(desktop->QueryInterface(IID_PPV_ARGS(&provider)));
    wil::com_ptr_nothrow<IShellBrowser> browser;
    RETURN_IF_FAILED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser)));
    wil::com_ptr_nothrow<IShellView> view;
    RETURN_IF_FAILED(browser->QueryActiveShellView(&view));
    wil::com_ptr_nothrow<IDispatch> background;
    RETURN_IF_FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background)));
    wil::com_ptr_nothrow<IShellFolderViewDual> folderView;
    RETURN_IF_FAILED(background->QueryInterface(IID_PPV_ARGS(&folderView)));
    wil::com_ptr_nothrow<IDispatch> application;
    RETURN_IF_FAILED(folderView->get_Application(&application));
    wil::com_ptr_nothrow<IShellDispatch2> shell;
    RETURN_IF_FAILED(application->QueryInterface(IID_PPV_ARGS(&shell)));

    const wil::unique_bstr file(SysAllocString(target.c_str()));
    RETURN_IF_NULL_ALLOC(file);
    wil::unique_variant arguments;
    wil::unique_variant directory;
    wil::unique_variant operation;
    wil::unique_variant show;
    show.vt = VT_I4;
    show.lVal = SW_SHOWNORMAL;
    return shell->ShellExecute(file.get(), arguments, directory, operation, show);
}

HRESULT LaunchDirectly(const std::wstring& target) noexcept
{
    SHELLEXECUTEINFOW info{sizeof(info)};
    // No error dialogs from the dock: failures are logged.
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpFile = target.c_str();
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

} // namespace

std::wstring LaunchTarget(const ipc::DockItem& item)
{
    const std::wstring target = Widen(item.target);
    return item.kind == ipc::DockItemKind::App ? L"shell:AppsFolder\\" + target : target;
}

LaunchResult LaunchApp(const ipc::DockItem& item) noexcept
try
{
    const std::wstring target = LaunchTarget(item);
    LaunchResult result;
    result.explorerResult = LaunchThroughExplorer(target);
    if (SUCCEEDED(result.explorerResult))
    {
        result.result = result.explorerResult;
        return result;
    }
    result.route = LaunchRoute::Direct;
    result.result = LaunchDirectly(target);
    return result;
}
catch (...)
{
    return {E_OUTOFMEMORY, LaunchRoute::Direct, E_OUTOFMEMORY};
}

} // namespace harbor::adapters
