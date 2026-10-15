#include "adapters/uia_island.h"

#include <windows.h>

// UI Automation's headers need OLE; winrt/base.h implements classic COM interfaces only if IUnknown came
// first.
#include <ole2.h>

#include <map>
#include <UIAutomation.h>
#include <utility>
#include <winrt/base.h>

namespace harbor::adapters
{
namespace
{

// Everything the providers see. UI thread only (ProviderOptions_UseComThreading brings UIA's calls there).
struct UiaState
{
    HWND window = nullptr;
    // Null once disconnected: every provider then answers UIA_E_ELEMENTNOTAVAILABLE.
    IIslandAccess* island = nullptr;
    int nextRuntimeId = 1;
    winrt::com_ptr<IRawElementProviderSimple> root;
    // Item providers by item key, made when first asked for and kept while their item stays.
    std::map<std::uint64_t, winrt::com_ptr<IRawElementProviderSimple>> items;
};

constexpr ProviderOptions kOptions =
    static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);

void SetString(VARIANT* value, const wchar_t* text) noexcept
{
    value->bstrVal = SysAllocString(text);
    value->vt = static_cast<VARTYPE>(value->bstrVal != nullptr ? VT_BSTR : VT_EMPTY);
}

HRESULT AsFragment(const winrt::com_ptr<IRawElementProviderSimple>& provider,
                   IRawElementProviderFragment** fragment) noexcept
{
    return provider ? provider->QueryInterface(IID_PPV_ARGS(fragment)) : S_OK;
}

winrt::com_ptr<IRawElementProviderSimple> ItemProvider(const std::shared_ptr<UiaState>& state,
                                                       std::size_t index);

// C++/WinRT implementation types can't be final.
class IslandItem
    : public winrt::implements<IslandItem, IRawElementProviderSimple, IRawElementProviderFragment,
                               IInvokeProvider, IExpandCollapseProvider>
{
  public:
    IslandItem(std::shared_ptr<UiaState> state, std::uint64_t key, int runtimeId)
        : m_state(std::move(state)), m_key(key), m_runtimeId(runtimeId)
    {
    }

    // IRawElementProviderSimple
    HRESULT __stdcall get_ProviderOptions(ProviderOptions* options) noexcept override
    {
        *options = kOptions;
        return S_OK;
    }
    HRESULT __stdcall GetPatternProvider(PATTERNID pattern, IUnknown** provider) noexcept override
    {
        *provider = nullptr;
        if (!Index())
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (pattern == UIA_InvokePatternId)
        {
            *provider = static_cast<IInvokeProvider*>(this);
            (*provider)->AddRef();
        }
        else if (pattern == UIA_ExpandCollapsePatternId && m_state->island->IsOverflow(*Index()))
        {
            // The "…" button: a button that expands into the overflow menu.
            *provider = static_cast<IExpandCollapseProvider*>(this);
            (*provider)->AddRef();
        }
        return S_OK;
    }
    HRESULT __stdcall GetPropertyValue(PROPERTYID property, VARIANT* value) noexcept override
    try
    {
        value->vt = VT_EMPTY;
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        switch (property)
        {
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = UIA_ButtonControlTypeId;
            break;
        case UIA_NamePropertyId:
            SetString(value, m_state->island->ItemName(*index).c_str());
            break;
        case UIA_AutomationIdPropertyId:
            SetString(value, (L"HarborDockItem." + std::to_wstring(m_key)).c_str());
            break;
        case UIA_ItemStatusPropertyId:
            if (const auto status = m_state->island->ItemStatus(*index); !status.empty())
            {
                SetString(value, status.c_str());
            }
            break;
        case UIA_IsKeyboardFocusablePropertyId:
            // The island never takes the focus.
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_FALSE;
            break;
        default:
            break;
        }
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT __stdcall get_HostRawElementProvider(IRawElementProviderSimple** host) noexcept override
    {
        *host = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT __stdcall Navigate(NavigateDirection direction,
                               IRawElementProviderFragment** element) noexcept override
    try
    {
        *element = nullptr;
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        switch (direction)
        {
        case NavigateDirection_Parent:
            return AsFragment(m_state->root, element);
        case NavigateDirection_NextSibling:
            return *index + 1 < m_state->island->ItemCount()
                       ? AsFragment(ItemProvider(m_state, *index + 1), element)
                       : S_OK;
        case NavigateDirection_PreviousSibling:
            return *index > 0 ? AsFragment(ItemProvider(m_state, *index - 1), element) : S_OK;
        default:
            return S_OK;
        }
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT __stdcall GetRuntimeId(SAFEARRAY** id) noexcept override
    {
        *id = nullptr;
        if (!Index())
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *id = SafeArrayCreateVector(VT_I4, 0, 2);
        if (*id == nullptr)
        {
            return E_OUTOFMEMORY;
        }
        const int parts[] = {UiaAppendRuntimeId, m_runtimeId};
        for (LONG i = 0; i < 2; ++i)
        {
            SafeArrayPutElement(*id, &i, const_cast<int*>(&parts[i]));
        }
        return S_OK;
    }
    HRESULT __stdcall get_BoundingRectangle(UiaRect* rect) noexcept override
    {
        *rect = {};
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const RECT bounds = m_state->island->ItemBounds(*index);
        *rect = {static_cast<double>(bounds.left), static_cast<double>(bounds.top),
                 static_cast<double>(bounds.right - bounds.left),
                 static_cast<double>(bounds.bottom - bounds.top)};
        return S_OK;
    }
    HRESULT __stdcall GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override
    {
        *roots = nullptr;
        return S_OK;
    }
    HRESULT __stdcall SetFocus() noexcept override
    {
        return S_OK;
    }
    HRESULT __stdcall get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override
    {
        *root = nullptr;
        if (!Index())
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return m_state->root ? m_state->root->QueryInterface(IID_PPV_ARGS(root)) : S_OK;
    }

    // IExpandCollapseProvider (the "…" button only)
    HRESULT __stdcall Expand() noexcept override
    {
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (!m_state->island->IsOverflow(*index))
        {
            return UIA_E_INVALIDOPERATION;
        }
        return m_state->island->InvokeItem(*index) ? S_OK : E_FAIL;
    }
    HRESULT __stdcall Collapse() noexcept override
    {
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (m_state->island->IsOverflow(*index) && m_state->island->IsOverflowMenuOpen())
        {
            m_state->island->CloseMenu();
        }
        return S_OK;
    }
    HRESULT __stdcall get_ExpandCollapseState(ExpandCollapseState* state) noexcept override
    {
        *state = ExpandCollapseState_LeafNode;
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (m_state->island->IsOverflow(*index))
        {
            *state = m_state->island->IsOverflowMenuOpen() ? ExpandCollapseState_Expanded
                                                           : ExpandCollapseState_Collapsed;
        }
        return S_OK;
    }

    // IInvokeProvider
    HRESULT __stdcall Invoke() noexcept override
    {
        const auto index = Index();
        if (!index)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (!m_state->island->InvokeItem(*index))
        {
            return E_FAIL;
        }
        if (UiaClientsAreListening())
        {
            UiaRaiseAutomationEvent(this, UIA_Invoke_InvokedEventId);
        }
        return S_OK;
    }

  private:
    // Where this provider's item is now; nullopt once it's gone or the island went away.
    std::optional<std::size_t> Index() const noexcept
    {
        return m_state->island != nullptr ? m_state->island->IndexOfKey(m_key) : std::nullopt;
    }

    std::shared_ptr<UiaState> m_state;
    std::uint64_t m_key;
    int m_runtimeId;
};

class IslandRoot : public winrt::implements<IslandRoot, IRawElementProviderSimple,
                                            IRawElementProviderFragment, IRawElementProviderFragmentRoot>
{
  public:
    explicit IslandRoot(std::shared_ptr<UiaState> state) : m_state(std::move(state))
    {
    }

    // IRawElementProviderSimple
    HRESULT __stdcall get_ProviderOptions(ProviderOptions* options) noexcept override
    {
        *options = kOptions;
        return S_OK;
    }
    HRESULT __stdcall GetPatternProvider(PATTERNID, IUnknown** provider) noexcept override
    {
        *provider = nullptr;
        return S_OK;
    }
    HRESULT __stdcall GetPropertyValue(PROPERTYID property, VARIANT* value) noexcept override
    {
        value->vt = VT_EMPTY;
        if (m_state->island == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        switch (property)
        {
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = UIA_ToolBarControlTypeId;
            break;
        case UIA_NamePropertyId:
            SetString(value, L"Harbor Dock");
            break;
        case UIA_AutomationIdPropertyId:
            SetString(value, L"HarborDock");
            break;
        case UIA_IsKeyboardFocusablePropertyId:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_FALSE;
            break;
        default:
            break;
        }
        return S_OK;
    }
    HRESULT __stdcall get_HostRawElementProvider(IRawElementProviderSimple** host) noexcept override
    {
        *host = nullptr;
        if (m_state->island == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        // The window's own provider supplies the position, runtime id and parent.
        return UiaHostProviderFromHwnd(m_state->window, host);
    }

    // IRawElementProviderFragment
    HRESULT __stdcall Navigate(NavigateDirection direction,
                               IRawElementProviderFragment** element) noexcept override
    try
    {
        *element = nullptr;
        if (m_state->island == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const auto count = m_state->island->ItemCount();
        if (count == 0)
        {
            return S_OK;
        }
        if (direction == NavigateDirection_FirstChild)
        {
            return AsFragment(ItemProvider(m_state, 0), element);
        }
        if (direction == NavigateDirection_LastChild)
        {
            return AsFragment(ItemProvider(m_state, count - 1), element);
        }
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    // The host's (the window's) runtime id and rectangle stand for the root.
    HRESULT __stdcall GetRuntimeId(SAFEARRAY** id) noexcept override
    {
        *id = nullptr;
        return S_OK;
    }
    HRESULT __stdcall get_BoundingRectangle(UiaRect* rect) noexcept override
    {
        *rect = {};
        return S_OK;
    }
    HRESULT __stdcall GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override
    {
        *roots = nullptr;
        return S_OK;
    }
    HRESULT __stdcall SetFocus() noexcept override
    {
        return S_OK;
    }
    HRESULT __stdcall get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override
    {
        *root = static_cast<IRawElementProviderFragmentRoot*>(this);
        (*root)->AddRef();
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    HRESULT __stdcall ElementProviderFromPoint(double x, double y,
                                               IRawElementProviderFragment** element) noexcept override
    try
    {
        *element = nullptr;
        if (m_state->island == nullptr)
        {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (const auto index = m_state->island->ItemAt({static_cast<LONG>(x), static_cast<LONG>(y)}))
        {
            return AsFragment(ItemProvider(m_state, *index), element);
        }
        // Between items, or with none: the island itself.
        *element = static_cast<IRawElementProviderFragment*>(this);
        (*element)->AddRef();
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
    HRESULT __stdcall GetFocus(IRawElementProviderFragment** element) noexcept override
    {
        *element = nullptr;
        return S_OK;
    }

  private:
    std::shared_ptr<UiaState> m_state;
};

winrt::com_ptr<IRawElementProviderSimple> ItemProvider(const std::shared_ptr<UiaState>& state,
                                                       std::size_t index)
{
    const auto key = state->island->ItemKey(index);
    auto& provider = state->items[key];
    if (!provider)
    {
        provider =
            winrt::make_self<IslandItem>(state, key, state->nextRuntimeId++).as<IRawElementProviderSimple>();
    }
    return provider;
}

} // namespace

struct IslandAccessibility::Shared : UiaState
{
};

IslandAccessibility::IslandAccessibility(HWND window, IIslandAccess& island)
    : m_shared(std::make_shared<Shared>())
{
    m_shared->window = window;
    m_shared->island = &island;
}

IslandAccessibility::~IslandAccessibility()
{
    Disconnect();
}

std::optional<LRESULT> IslandAccessibility::OnGetObject(WPARAM wParam, LPARAM lParam)
{
    if (static_cast<LONG>(lParam) != static_cast<LONG>(UiaRootObjectId) || m_shared->island == nullptr)
    {
        return std::nullopt;
    }
    if (!m_shared->root)
    {
        // Made only once a client asks: until then UIAutomationCore.dll isn't even loaded (it is
        // delay-loaded).
        m_shared->root = winrt::make_self<IslandRoot>(std::static_pointer_cast<UiaState>(m_shared))
                             .as<IRawElementProviderSimple>();
    }
    return UiaReturnRawElementProvider(m_shared->window, wParam, lParam, m_shared->root.get());
}

void IslandAccessibility::ItemsChanged()
{
    if (m_shared->island == nullptr)
    {
        return;
    }
    // Items that moved keep their providers (and a screen reader its place); those that are gone go dead.
    auto& items = m_shared->items;
    for (auto it = items.begin(); it != items.end();)
    {
        if (m_shared->island->IndexOfKey(it->first))
        {
            ++it;
            continue;
        }
        if (m_shared->root)
        {
            UiaDisconnectProvider(it->second.get());
        }
        it = items.erase(it);
    }
    if (!m_shared->root)
    {
        return;
    }
    if (UiaClientsAreListening())
    {
        UiaRaiseStructureChangedEvent(m_shared->root.get(), StructureChangeType_ChildrenInvalidated, nullptr,
                                      0);
    }
}

void IslandAccessibility::Disconnect() noexcept
{
    if (m_shared->island == nullptr)
    {
        return;
    }
    m_shared->island = nullptr;
    if (!m_shared->root)
    {
        return;
    }
    for (const auto& [key, item] : m_shared->items)
    {
        UiaDisconnectProvider(item.get());
    }
    m_shared->items.clear();
    UiaDisconnectProvider(m_shared->root.get());
    // Lets go of the root UiaReturnRawElementProvider handed out.
    UiaReturnRawElementProvider(m_shared->window, 0, 0, nullptr);
    m_shared->root = nullptr;
}

} // namespace harbor::adapters
