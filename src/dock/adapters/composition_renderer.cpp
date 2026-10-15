#include "adapters/composition_renderer.h"

#include <algorithm>
#include <atomic>
#include <d2d1_1.h>
#include <d3d11_4.h>
#include <DispatcherQueue.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <format>
#include <string>
#include <vector>
#include <wil/resource.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>

namespace harbor::adapters
{
namespace
{

namespace wuc = winrt::Windows::UI::Composition;
namespace abi = ABI::Windows::UI::Composition;
using winrt::Windows::Foundation::Numerics::float2;

winrt::Windows::UI::Color ToWinRt(island::Color color) noexcept
{
    return {color.a, color.r, color.g, color.b};
}

D2D1_COLOR_F ToD2D(island::Color color) noexcept
{
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

float2 Size(const geometry::Rect& rect) noexcept
{
    return {static_cast<float>(rect.Width()), static_cast<float>(rect.Height())};
}

// WARP keeps the dock off the GPU driver: its few static surfaces don't need one, and it costs less memory
// (docs/adr/0008). Hardware only if WARP is missing.
winrt::com_ptr<ID2D1Device> CreateD2DDevice(std::string& kind, winrt::com_ptr<ID3D11Device>& d3d)
{
    HRESULT result = E_FAIL;
    for (const auto type : {D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_HARDWARE})
    {
        d3d = nullptr;
        result = D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr);
        if (SUCCEEDED(result))
        {
            kind = type == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware";
            break;
        }
    }
    winrt::check_hresult(result);
    winrt::com_ptr<ID2D1Device> d2d;
    winrt::check_hresult(D2D1CreateDevice(d3d.as<IDXGIDevice>().get(), nullptr, d2d.put()));
    return d2d;
}

std::string Describe(const winrt::hresult_error& error)
{
    return std::format("0x{:08X}", static_cast<std::uint32_t>(error.code().value));
}

} // namespace

struct CompositionRenderer::Impl
{
    explicit Impl(log::ILogger& logger) : log(logger)
    {
    }

    ~Impl()
    {
        StopWatchingDevice();
    }

    // Creates a Direct2D device and makes it the one composition draws with; the first time, that creates the
    // graphics device. Throws winrt::hresult_error.
    void UseNewDevice()
    {
        StopWatchingDevice();
        std::string kind;
        winrt::com_ptr<ID3D11Device> d3d;
        const auto d2d = CreateD2DDevice(kind, d3d);
        if (!graphics)
        {
            winrt::check_hresult(compositor.as<abi::ICompositorInterop>()->CreateGraphicsDevice(
                d2d.get(), reinterpret_cast<abi::ICompositionGraphicsDevice**>(winrt::put_abi(graphics))));
        }
        else
        {
            winrt::check_hresult(
                graphics.as<abi::ICompositionGraphicsDeviceInterop>()->SetRenderingDevice(d2d.get()));
        }
        WatchDevice(d3d);
        log.Info(std::format("composition: drawing with a {} graphics device", kind));
    }

    // Asks Direct3D to signal an event if the device goes away (a driver update or reset; WARP practically
    // never), and posts deviceLostMessage to the window when it does.
    void WatchDevice(const winrt::com_ptr<ID3D11Device>& d3d)
    {
        if (!d3d.try_as(device) || !removed.try_create(wil::EventOptions::None, nullptr) ||
            FAILED(device->RegisterDeviceRemovedEvent(removed.get(), &removedCookie)))
        {
            device = nullptr;
            return;
        }
        removedWait.reset(CreateThreadpoolWait(
            [](PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) {
                const auto* self = static_cast<const Impl*>(context);
                PostMessageW(self->window.load(), self->deviceLostMessage.load(), 0, 0);
            },
            this, nullptr));
        if (removedWait)
        {
            SetThreadpoolWait(removedWait.get(), removed.get(), nullptr);
        }
    }

    void StopWatchingDevice() noexcept
    {
        // Waits for a callback in progress.
        removedWait.reset();
        if (device)
        {
            device->UnregisterDeviceRemoved(removedCookie);
            device = nullptr;
        }
        removed.reset();
    }

    // A size x size surface drawn by draw(context, area): area is the surface's square in the context's
    // pixels.
    template <typename Draw> wuc::CompositionSurfaceBrush DrawSurface(int size, Draw&& draw)
    {
        const float extent = static_cast<float>(size);
        auto surface = graphics.CreateDrawingSurface(
            {extent, extent}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        const auto interop = surface.as<abi::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> context;
        POINT offset{};
        winrt::check_hresult(interop->BeginDraw(nullptr, IID_PPV_ARGS(context.put()), &offset));
        context->Clear({0, 0, 0, 0});
        const D2D1_RECT_F area{static_cast<float>(offset.x), static_cast<float>(offset.y),
                               static_cast<float>(offset.x) + extent, static_cast<float>(offset.y) + extent};
        draw(*context, area);
        winrt::check_hresult(interop->EndDraw());
        return compositor.CreateSurfaceBrush(surface);
    }

    // An item without an icon: a rounded square in the placeholder color.
    wuc::CompositionSurfaceBrush PlaceholderBrush(int size, island::Color color)
    {
        return DrawSurface(size, [&](ID2D1DeviceContext& context, const D2D1_RECT_F& area) {
            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            winrt::check_hresult(context.CreateSolidColorBrush(ToD2D(color), brush.put()));
            const float inset = (area.right - area.left) * 0.08f;
            const float radius = (area.right - area.left) * 0.22f;
            context.FillRoundedRectangle(
                {{area.left + inset, area.top + inset, area.right - inset, area.bottom - inset},
                 radius,
                 radius},
                brush.get());
        });
    }

    // The Shell's icon, scaled down to size (high-quality cubic), centred if it isn't square.
    wuc::CompositionSurfaceBrush IconBrush(int size, const island::IconView& icon)
    {
        return DrawSurface(size, [&](ID2D1DeviceContext& context, const D2D1_RECT_F& area) {
            const D2D1_BITMAP_PROPERTIES properties{
                {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f};
            winrt::com_ptr<ID2D1Bitmap> bitmap;
            winrt::check_hresult(context.CreateBitmap({icon.width, icon.height}, icon.pixels.data(),
                                                      icon.width * 4, properties, bitmap.put()));
            const float extent = area.right - area.left;
            const float scale = extent / static_cast<float>(std::max(icon.width, icon.height));
            const float width = icon.width * scale;
            const float height = icon.height * scale;
            const float left = area.left + (extent - width) / 2;
            const float top = area.top + (extent - height) / 2;
            context.DrawBitmap(bitmap.get(), D2D1_RECT_F{left, top, left + width, top + height}, 1.0f,
                               D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        });
    }

    log::ILogger& log;
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    wuc::Compositor compositor{nullptr};
    wuc::CompositionGraphicsDevice graphics{nullptr};
    wuc::Desktop::DesktopWindowTarget target{nullptr};
    wuc::ContainerVisual root{nullptr};
    wuc::SpriteVisual hover{nullptr};
    // Read by the device-removed callback on a thread-pool thread.
    std::atomic<HWND> window{nullptr};
    std::atomic<UINT> deviceLostMessage{0};
    // Window-relative item rectangles and the hover highlight's margin around one, for SetHovered.
    std::vector<geometry::Rect> items;
    int hoverInset = 0;

    winrt::com_ptr<ID3D11Device4> device;
    DWORD removedCookie = 0;
    wil::unique_event_nothrow removed;
    // Declared after the event, so it is closed (and its callback finished) first.
    wil::unique_threadpool_wait removedWait;
};

CompositionRenderer::CompositionRenderer(log::ILogger& log) : m_impl(std::make_unique<Impl>(log))
{
}

CompositionRenderer::~CompositionRenderer()
{
    try
    {
        // The window target first, while the compositor and its queue still exist.
        m_impl->target = nullptr;
        m_impl->root = nullptr;
        m_impl->hover = nullptr;
        m_impl->graphics = nullptr;
        m_impl->compositor = nullptr;
        if (m_impl->queue)
        {
            // Shutting the queue down asynchronously is enough: the thread's message loop is ending.
            m_impl->queue.ShutdownQueueAsync();
            m_impl->queue = nullptr;
        }
    }
    catch (...)
    {
    }
}

bool CompositionRenderer::Initialize() noexcept
try
{
    auto& impl = *m_impl;
    DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
    winrt::check_hresult(CreateDispatcherQueueController(
        options,
        reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(impl.queue))));
    impl.compositor = wuc::Compositor();
    impl.UseNewDevice();
    return true;
}
catch (const winrt::hresult_error& error)
{
    m_impl->log.Error("composition can't start: " + Describe(error));
    return false;
}
catch (...)
{
    m_impl->log.Error("composition can't start: unexpected error");
    return false;
}

bool CompositionRenderer::RecreateDevice() noexcept
try
{
    m_impl->UseNewDevice();
    return true;
}
catch (const winrt::hresult_error& error)
{
    m_impl->log.Error("composition: no new graphics device: " + Describe(error));
    return false;
}
catch (...)
{
    m_impl->log.Error("composition: no new graphics device: unexpected error");
    return false;
}

bool CompositionRenderer::Attach(HWND window, UINT deviceLostMessage) noexcept
try
{
    auto& impl = *m_impl;
    impl.deviceLostMessage.store(deviceLostMessage);
    // The host backdrop brush only shows Windows' blur for a window that asks for it.
    const BOOL useHostBackdrop = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_HOSTBACKDROPBRUSH, &useHostBackdrop, sizeof(useHostBackdrop));
    // Our own corners, not Windows 11's.
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    winrt::check_hresult(
        impl.compositor.as<abi::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
            window, TRUE,
            reinterpret_cast<abi::Desktop::IDesktopWindowTarget**>(winrt::put_abi(impl.target))));
    impl.root = impl.compositor.CreateContainerVisual();
    impl.root.RelativeSizeAdjustment({1, 1});
    impl.target.Root(impl.root);
    impl.window.store(window);
    return true;
}
catch (const winrt::hresult_error& error)
{
    m_impl->log.Error("composition can't draw into the dock window: " + Describe(error));
    return false;
}
catch (...)
{
    m_impl->log.Error("composition can't draw into the dock window: unexpected error");
    return false;
}

bool CompositionRenderer::Render(const island::Layout& layout, const island::IslandStyle& style,
                                 std::span<const std::optional<island::IconView>> icons,
                                 std::optional<std::size_t> hovered, UINT committedMessage,
                                 WPARAM tag) noexcept
try
{
    auto& impl = *m_impl;
    const auto& metrics = layout.metrics;
    const geometry::Rect bounds{0, 0, layout.island.Width(), layout.island.Height()};
    const float radius = static_cast<float>(metrics.cornerRadius);
    const float2 corner{radius, radius};

    impl.root.Children().RemoveAll();
    auto island = impl.compositor.CreateContainerVisual();
    island.Size(Size(bounds));
    island.Clip(impl.compositor.CreateRectangleClip(0, 0, Size(bounds).x, Size(bounds).y, corner, corner,
                                                    corner, corner));
    impl.root.Children().InsertAtTop(island);

    if (style.backdrop)
    {
        auto blur = impl.compositor.CreateSpriteVisual();
        blur.RelativeSizeAdjustment({1, 1});
        blur.Brush(impl.compositor.CreateHostBackdropBrush());
        island.Children().InsertAtTop(blur);
    }
    auto fill = impl.compositor.CreateSpriteVisual();
    fill.RelativeSizeAdjustment({1, 1});
    fill.Brush(impl.compositor.CreateColorBrush(ToWinRt(style.fill)));
    island.Children().InsertAtTop(fill);

    // The hover highlight: a rounded square a little larger than the icon.
    impl.hoverInset = metrics.padding / 2;
    const float hoverSize = static_cast<float>(metrics.icon + 2 * impl.hoverInset);
    const float hoverRadius = hoverSize * 0.2f;
    impl.hover = impl.compositor.CreateSpriteVisual();
    impl.hover.Size({hoverSize, hoverSize});
    impl.hover.Brush(impl.compositor.CreateColorBrush(ToWinRt(style.hover)));
    impl.hover.Clip(impl.compositor.CreateRectangleClip(
        0, 0, hoverSize, hoverSize, {hoverRadius, hoverRadius}, {hoverRadius, hoverRadius},
        {hoverRadius, hoverRadius}, {hoverRadius, hoverRadius}));
    island.Children().InsertAtTop(impl.hover);

    impl.items.clear();
    // One surface serves every item without an icon; made only if one needs it.
    wuc::CompositionSurfaceBrush placeholder{nullptr};
    for (std::size_t i = 0; i < layout.items.size(); ++i)
    {
        const auto& item = layout.items[i];
        const geometry::Rect local{item.left - layout.island.left, item.top - layout.island.top,
                                   item.right - layout.island.left, item.bottom - layout.island.top};
        impl.items.push_back(local);
        auto visual = impl.compositor.CreateSpriteVisual();
        visual.Size(Size(local));
        visual.Offset({static_cast<float>(local.left), static_cast<float>(local.top), 0});
        if (i < icons.size() && icons[i])
        {
            visual.Brush(impl.IconBrush(metrics.icon, *icons[i]));
        }
        else
        {
            if (!placeholder)
            {
                placeholder = impl.PlaceholderBrush(metrics.icon, style.placeholder);
            }
            visual.Brush(placeholder);
        }
        island.Children().InsertAtTop(visual);
    }

    // A hairline inside the edge.
    auto outline = impl.compositor.CreateRoundedRectangleGeometry();
    outline.Offset({0.5f, 0.5f});
    outline.Size({Size(bounds).x - 1, Size(bounds).y - 1});
    outline.CornerRadius({radius - 0.5f, radius - 0.5f});
    auto stroke = impl.compositor.CreateSpriteShape(outline);
    stroke.StrokeBrush(impl.compositor.CreateColorBrush(ToWinRt(style.border)));
    stroke.StrokeThickness(1);
    auto border = impl.compositor.CreateShapeVisual();
    border.Size(Size(bounds));
    border.Shapes().Append(stroke);
    island.Children().InsertAtTop(border);

    SetHovered(hovered);

    const HWND window = impl.window.load();
    impl.compositor.RequestCommitAsync().Completed(
        [window, committedMessage, tag](const auto&, winrt::Windows::Foundation::AsyncStatus status) {
            if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
            {
                PostMessageW(window, committedMessage, tag, 0);
            }
        });
    return true;
}
catch (const winrt::hresult_error& error)
{
    m_impl->log.Error("couldn't draw the island: " + Describe(error));
    return false;
}
catch (...)
{
    m_impl->log.Error("couldn't draw the island: unexpected error");
    return false;
}

void CompositionRenderer::SetHovered(std::optional<std::size_t> hovered) noexcept
try
{
    auto& impl = *m_impl;
    if (!impl.hover)
    {
        return;
    }
    if (!hovered || *hovered >= impl.items.size())
    {
        impl.hover.IsVisible(false);
        return;
    }
    const auto& item = impl.items[*hovered];
    impl.hover.Offset(
        {static_cast<float>(item.left - impl.hoverInset), static_cast<float>(item.top - impl.hoverInset), 0});
    impl.hover.IsVisible(true);
}
catch (...)
{
    m_impl->log.Warn("couldn't move the hover highlight");
}

} // namespace harbor::adapters
