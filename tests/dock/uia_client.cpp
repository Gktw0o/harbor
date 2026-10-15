// A UI Automation client for the island's end-to-end test, as its own process (as a screen reader would be).
//
// harbor_test_uia_client <island window> list
//     Prints "<control type id>\t<name>" for the island, then a line for each child (UTF-8).
// harbor_test_uia_client <island window> invoke <name>
//     Invokes the child button with that name.
// harbor_test_uia_client <island window> hold <name>
//     Finds the child button with that name and prints "found". After a line on standard input, asks that
//     same element for its name again and prints "alive\t<name>", or "gone" if it is no longer available.
// harbor_test_uia_client <island window> status <name>
//     Prints "status\t<item status>" for the child button with that name (empty if it has none).
//
// Exit codes: 0 done, 1 bad arguments, 2 UI Automation failed, 3 no such button, 4 Invoke failed, 5 gone.

#include <windows.h>

#include <ole2.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <UIAutomation.h>
#include <wil/com.h>
#include <wil/resource.h>

namespace
{

void PrintLine(const std::wstring& first, const wchar_t* name)
{
    const std::wstring line = first + L"\t" + (name != nullptr ? name : L"") + L"\n";
    const int length = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), length, nullptr,
                        nullptr);
    std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    std::fflush(stdout);
}

void PrintLine(int controlType, const wchar_t* name)
{
    PrintLine(std::to_wstring(controlType), name);
}

// The hold command, once the button is found: waits for a line, then asks the same element again.
int Hold(IUIAutomationElement& element)
{
    std::fputs("found\n", stdout);
    std::fflush(stdout);
    char line[16]{};
    std::fgets(line, sizeof(line), stdin);
    wil::unique_bstr name;
    const HRESULT result = element.get_CurrentName(&name);
    if (result == UIA_E_ELEMENTNOTAVAILABLE)
    {
        std::fputs("gone\n", stdout);
        return 5;
    }
    if (FAILED(result))
    {
        return 2;
    }
    PrintLine(L"alive", name.get());
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3 || (std::wstring(argv[2]) != L"list" && argc < 4))
    {
        return 1;
    }
    const auto island =
        reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::wcstoull(argv[1], nullptr, 10)));
    const std::wstring command = argv[2];

    const auto com = wil::CoInitializeEx_failfast(COINIT_MULTITHREADED);
    wil::com_ptr_nothrow<IUIAutomation> automation;
    wil::com_ptr_nothrow<IUIAutomationElement> root;
    wil::com_ptr_nothrow<IUIAutomationCondition> all;
    wil::com_ptr_nothrow<IUIAutomationElementArray> children;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation))) ||
        FAILED(automation->ElementFromHandle(island, &root)) || !root ||
        FAILED(automation->CreateTrueCondition(&all)) ||
        FAILED(root->FindAll(TreeScope_Children, all.get(), &children)) || !children)
    {
        return 2;
    }

    if (command == L"list")
    {
        CONTROLTYPEID type = 0;
        wil::unique_bstr name;
        root->get_CurrentControlType(&type);
        root->get_CurrentName(&name);
        PrintLine(type, name.get());
    }

    int count = 0;
    children->get_Length(&count);
    for (int i = 0; i < count; ++i)
    {
        wil::com_ptr_nothrow<IUIAutomationElement> child;
        CONTROLTYPEID type = 0;
        wil::unique_bstr name;
        if (FAILED(children->GetElement(i, &child)) || FAILED(child->get_CurrentControlType(&type)) ||
            FAILED(child->get_CurrentName(&name)))
        {
            return 2;
        }
        if (command == L"list")
        {
            PrintLine(type, name.get());
        }
        else if (type == UIA_ButtonControlTypeId && name && std::wstring(name.get()) == argv[3])
        {
            if (command == L"hold")
            {
                return Hold(*child);
            }
            if (command == L"status")
            {
                wil::unique_bstr status;
                if (FAILED(child->get_CurrentItemStatus(&status)))
                {
                    return 2;
                }
                PrintLine(L"status", status.get());
                return 0;
            }
            wil::com_ptr_nothrow<IUIAutomationInvokePattern> invoke;
            if (FAILED(child->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))) || !invoke)
            {
                return 4;
            }
            return SUCCEEDED(invoke->Invoke()) ? 0 : 4;
        }
    }
    return command == L"list" ? 0 : 3;
}
