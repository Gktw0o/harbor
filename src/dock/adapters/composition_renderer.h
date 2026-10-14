#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <optional>

#include "core/island/appearance.h"
#include "core/island/layout.h"
#include "ports/logger.h"

namespace harbor::adapters
{

// Draws the island with Windows.UI.Composition (docs/adr/0008): the host backdrop (Windows' own blur of what
// is behind the window) under a tint, clipped to a rounded rectangle, with Direct2D-drawn item surfaces on a
// WARP device. Everything happens on the thread that called Initialize, which must pump messages: the
// compositor's dispatcher queue runs on it. Composition errors are caught and logged here, never thrown.
class CompositionRenderer
{
  public:
    explicit CompositionRenderer(log::ILogger& log);
    ~CompositionRenderer();
    CompositionRenderer(const CompositionRenderer&) = delete;
    CompositionRenderer& operator=(const CompositionRenderer&) = delete;

    // Creates this thread's dispatcher queue, the compositor and the graphics device. COM must be initialized
    // (STA) on the thread. False if any of it fails: the dock can't draw at all.
    bool Initialize() noexcept;

    // Draws into window, which must have WS_EX_NOREDIRECTIONBITMAP and must not be destroyed before this
    // renderer. False if the window can't be drawn into. deviceLostMessage is posted to the window if the
    // graphics device goes away; then RecreateDevice and Render again.
    bool Attach(HWND window, UINT deviceLostMessage) noexcept;

    // A new graphics device after the old one was lost (or, in tests, to rehearse that). What was drawn is
    // gone: Render again. False if there is none to be had.
    bool RecreateDevice() noexcept;

    // Rebuilds the island for layout (screen coordinates; the window covers layout.island) and style. Posts
    // committedMessage (wParam = tag) to the window once the frame has been committed for the screen. False
    // if it failed.
    bool Render(const island::Layout& layout, const island::IslandStyle& style,
                std::optional<std::size_t> hovered, UINT committedMessage, WPARAM tag) noexcept;

    // Moves the hover highlight; nullopt hides it.
    void SetHovered(std::optional<std::size_t> hovered) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace harbor::adapters
