#include "adapters/popup_menu.h"

// The accessibility headers need OLE.
#include <ole2.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <oleacc.h>
#include <string>
#include <wil/resource.h>

namespace harbor::adapters
{
namespace
{

// What an owner-drawn item carries. The MSAAMENUINFO comes first: that's where screen readers look for its
// text.
struct OwnerItem
{
    MSAAMENUINFO msaa{};
    std::wstring text;
    bool separator = false;
    bool title = false;
    // Premultiplied 32-bit, iconSide square; owned by the open menu.
    HBITMAP icon = nullptr;
};

// The open menu: its look, fonts, colors, items and their icons. One at a time, on the UI thread.
struct OpenMenu
{
    PopupMenu::Look look;
    wil::unique_hfont font;
    wil::unique_hfont bold;
    COLORREF background = 0;
    COLORREF highlight = 0;
    COLORREF text = 0;
    COLORREF dim = 0;
    int iconSide = 0;
    std::vector<std::unique_ptr<OwnerItem>> items;
    std::vector<wil::unique_hbitmap> icons;
};

// A top-down 32-bit DIB section holding pixels (premultiplied BGRA, side x side).
wil::unique_hbitmap IconBitmap(const std::vector<std::uint8_t>& pixels, int side)
{
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB};
    void* bits = nullptr;
    wil::unique_hbitmap bitmap(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (bitmap && bits != nullptr && pixels.size() == static_cast<std::size_t>(side) * side * 4)
    {
        std::memcpy(bits, pixels.data(), pixels.size());
        return bitmap;
    }
    return {};
}

thread_local OpenMenu* t_open = nullptr;

std::wstring Wide(const std::string& utf8)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
    return wide;
}

COLORREF Rgb(island::Color color) noexcept
{
    return RGB(color.r, color.g, color.b);
}

// over drawn at its alpha on base.
COLORREF Blend(COLORREF base, island::Color over, int alpha) noexcept
{
    const auto mix = [alpha](int from, int to) {
        return static_cast<BYTE>((from * (255 - alpha) + to * alpha) / 255);
    };
    return RGB(mix(GetRValue(base), over.r), mix(GetGValue(base), over.g), mix(GetBValue(base), over.b));
}

int Scale(int dip, unsigned dpi) noexcept
{
    return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

// A plain menu string shows "&" as a mnemonic marker: double it.
std::wstring Plain(const std::wstring& text)
{
    std::wstring plain;
    for (const wchar_t c : text)
    {
        plain += c;
        if (c == L'&')
        {
            plain += L'&';
        }
    }
    return plain;
}

void Append(HMENU menu, UINT id, OwnerItem& item, bool enabled, bool highContrast)
{
    item.msaa.dwMSAASignature = MSAA_MENU_SIG;
    item.msaa.cchWText = static_cast<DWORD>(item.text.size());
    item.msaa.pszWText = item.text.data();
    std::wstring plain = Plain(item.text);
    MENUITEMINFOW info{sizeof(info)};
    info.fMask =
        MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE | (highContrast && !item.separator ? MIIM_STRING : 0);
    info.fType = (highContrast ? 0 : MFT_OWNERDRAW) | (item.separator ? MFT_SEPARATOR : MFT_STRING);
    info.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
    info.wID = id;
    info.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
    info.dwTypeData = plain.data();
    if (highContrast && item.icon != nullptr)
    {
        // The system menu draws a premultiplied 32-bit bitmap with its alpha.
        info.fMask |= MIIM_BITMAP;
        info.hbmpItem = item.icon;
    }
    InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &info);
}

OwnerItem* ItemOf(ULONG_PTR data) noexcept
{
    if (t_open == nullptr || data == 0)
    {
        return nullptr;
    }
    // Only this menu's items: anything else is someone else's owner-drawn item.
    const auto* item = reinterpret_cast<OwnerItem*>(data);
    for (const auto& owned : t_open->items)
    {
        if (owned.get() == item)
        {
            return owned.get();
        }
    }
    return nullptr;
}

} // namespace

std::optional<std::size_t> PopupMenu::Show(HWND owner, const std::vector<island::MenuItem>& items,
                                           POINT anchor, const RECT& avoid, const Look& look,
                                           std::span<const std::vector<std::uint8_t>> icons, int iconSide)
{
    wil::unique_hmenu menu(CreatePopupMenu());
    if (!menu)
    {
        return std::nullopt;
    }
    OpenMenu open{.look = look, .iconSide = iconSide};
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, look.dpi))
    {
        open.font.reset(CreateFontIndirectW(&metrics.lfMenuFont));
        metrics.lfMenuFont.lfWeight = FW_SEMIBOLD;
        open.bold.reset(CreateFontIndirectW(&metrics.lfMenuFont));
    }
    open.background = Rgb(look.style.fill);
    open.highlight = Blend(open.background, look.style.hover, look.style.hover.a);
    open.text = Rgb(look.style.dot);
    open.dim = Blend(open.background, look.style.dot, 110);

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].separatorAbove && i > 0)
        {
            auto& separator =
                *open.items.emplace_back(std::make_unique<OwnerItem>(OwnerItem{.separator = true}));
            Append(menu.get(), 0, separator, /*enabled=*/false, look.highContrast);
        }
        auto& item = *open.items.emplace_back(std::make_unique<OwnerItem>(
            OwnerItem{.text = Wide(items[i].text), .title = items[i].command == island::MenuCommand::Title}));
        if (i < icons.size() && !icons[i].empty() && iconSide > 0)
        {
            if (auto bitmap = IconBitmap(icons[i], iconSide))
            {
                item.icon = bitmap.get();
                open.icons.push_back(std::move(bitmap));
            }
        }
        Append(menu.get(), static_cast<UINT>(i + 1), item, items[i].enabled, look.highContrast);
    }

    wil::unique_hbrush background;
    MENUINFO info{sizeof(info)};
    // No room for check marks: the dock's items never have them.
    info.fMask = MIM_STYLE;
    info.dwStyle = MNS_NOCHECK;
    if (!look.highContrast)
    {
        background.reset(CreateSolidBrush(open.background));
        info.fMask |= MIM_BACKGROUND;
        info.hbrBack = background.get();
    }
    SetMenuInfo(menu.get(), &info);

    // The menu only closes on a click elsewhere if its owner is the foreground window.
    const HWND previous = GetForegroundWindow();
    SetForegroundWindow(owner);
    t_open = &open;
    TPMPARAMS avoidIsland{sizeof(avoidIsland), avoid};
    const auto chosen = static_cast<UINT>(TrackPopupMenuEx(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_CENTERALIGN | TPM_VERTICAL,
        anchor.x, anchor.y, owner, &avoidIsland));
    t_open = nullptr;
    // Lets the menu's loop finish cleanly (the documented TrackPopupMenu pattern).
    PostMessageW(owner, WM_NULL, 0, 0);
    if (GetForegroundWindow() == owner && previous != nullptr && IsWindow(previous))
    {
        SetForegroundWindow(previous);
    }
    return chosen == 0 ? std::nullopt : std::optional<std::size_t>(chosen - 1);
}

bool PopupMenu::OnMeasureItem(MEASUREITEMSTRUCT& measure) noexcept
{
    const auto* item = measure.CtlType == ODT_MENU ? ItemOf(measure.itemData) : nullptr;
    if (item == nullptr)
    {
        return false;
    }
    const auto dpi = t_open->look.dpi;
    if (item->separator)
    {
        measure.itemWidth = 0;
        measure.itemHeight = static_cast<UINT>(Scale(9, dpi));
        return true;
    }
    const auto screen = wil::GetDC(nullptr);
    const auto select =
        wil::SelectObject(screen.get(), item->title ? t_open->bold.get() : t_open->font.get());
    SIZE size{};
    GetTextExtentPoint32W(screen.get(), item->text.c_str(), static_cast<int>(item->text.size()), &size);
    // Room for an icon before the text when the menu has icons.
    const int iconRoom = t_open->iconSide > 0 ? t_open->iconSide + Scale(10, dpi) : 0;
    measure.itemWidth = static_cast<UINT>(size.cx + iconRoom + 2 * Scale(14, dpi));
    measure.itemHeight =
        static_cast<UINT>(std::max(size.cy, static_cast<LONG>(t_open->iconSide)) + 2 * Scale(6, dpi));
    return true;
}

bool PopupMenu::OnDrawItem(const DRAWITEMSTRUCT& draw) noexcept
{
    const auto* item = draw.CtlType == ODT_MENU ? ItemOf(draw.itemData) : nullptr;
    if (item == nullptr)
    {
        return false;
    }
    const auto& open = *t_open;
    const auto dpi = open.look.dpi;
    const bool disabled = (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    const bool selected = (draw.itemState & ODS_SELECTED) != 0 && !disabled;
    RECT rect = draw.rcItem;
    const wil::unique_hbrush fill(CreateSolidBrush(selected ? open.highlight : open.background));
    FillRect(draw.hDC, &rect, fill.get());
    if (item->separator)
    {
        const int middle = (rect.top + rect.bottom) / 2;
        RECT line{rect.left + Scale(10, dpi), middle, rect.right - Scale(10, dpi),
                  middle + std::max(1, Scale(1, dpi))};
        const wil::unique_hbrush dim(CreateSolidBrush(open.dim));
        FillRect(draw.hDC, &line, dim.get());
        return true;
    }
    const auto select = wil::SelectObject(draw.hDC, item->title ? open.bold.get() : open.font.get());
    SetBkMode(draw.hDC, TRANSPARENT);
    // The title is disabled (it does nothing) but reads as a heading, not as greyed out.
    SetTextColor(draw.hDC, disabled && !item->title ? open.dim : open.text);
    rect.left += Scale(14, dpi);
    rect.right -= Scale(14, dpi);
    if (open.iconSide > 0)
    {
        if (item->icon != nullptr)
        {
            const wil::unique_hdc icons(CreateCompatibleDC(draw.hDC));
            const auto oldIcon = wil::SelectObject(icons.get(), item->icon);
            const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            const int top = (rect.top + rect.bottom - open.iconSide) / 2;
            GdiAlphaBlend(draw.hDC, rect.left, top, open.iconSide, open.iconSide, icons.get(), 0, 0,
                          open.iconSide, open.iconSide, blend);
        }
        rect.left += open.iconSide + Scale(10, dpi);
    }
    DrawTextW(draw.hDC, item->text.c_str(), static_cast<int>(item->text.size()), &rect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    return true;
}

std::optional<LRESULT> PopupMenu::OnMenuChar(WPARAM wParam, LPARAM lParam) noexcept
{
    const auto menu = reinterpret_cast<HMENU>(lParam);
    if (t_open == nullptr || menu == nullptr)
    {
        return std::nullopt;
    }
    const auto typed = static_cast<wchar_t>(LOWORD(wParam));
    const int count = GetMenuItemCount(menu);
    // After the highlighted item, round to the start.
    int start = 0;
    for (int i = 0; i < count; ++i)
    {
        if ((GetMenuState(menu, static_cast<UINT>(i), MF_BYPOSITION) & MF_HILITE) != 0)
        {
            start = i + 1;
            break;
        }
    }
    for (int step = 0; step < count; ++step)
    {
        const int position = (start + step) % count;
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_DATA | MIIM_STATE;
        if (!GetMenuItemInfoW(menu, static_cast<UINT>(position), TRUE, &info) ||
            (info.fState & MFS_DISABLED) != 0)
        {
            continue;
        }
        const auto* item = ItemOf(info.dwItemData);
        if (item != nullptr && !item->separator && !item->text.empty() &&
            CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(item->text.front()))) ==
                CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(typed))))
        {
            return MAKELRESULT(position, MNC_SELECT);
        }
    }
    return MAKELRESULT(0, MNC_IGNORE);
}

} // namespace harbor::adapters
