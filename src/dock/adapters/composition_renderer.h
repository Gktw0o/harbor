#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/island/appearance.h"
#include "core/island/icon_file.h"
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

    // Reads item index's icon into file and returns a view of it, or nullopt (a placeholder is drawn). Called
    // once per item, in order, with the same file each time: one icon's pixels at a time (rule 4).
    using IconSource =
        std::function<std::optional<island::IconView>(std::size_t index, std::vector<std::uint8_t>& file)>;

    // Rebuilds the island for layout (screen coordinates; the window covers layout.island) and style, with
    // the icons from icons and the marks as SetMarks would show them. Posts committedMessage (wParam = tag)
    // to the window once the frame has been committed for the screen. False if it failed.
    bool Render(const island::Layout& layout, const island::IslandStyle& style, const IconSource& icons,
                std::span<const island::ItemMarks> marks, std::optional<std::size_t> hovered,
                UINT committedMessage, WPARAM tag) noexcept;

    // Moves the hover highlight; nullopt hides it.
    void SetHovered(std::optional<std::size_t> hovered) noexcept;

    // Shows item i's running dot and attention highlight as marks[i] says (none past the end of marks).
    // Nothing is redrawn: the dots and highlights are there already, hidden.
    void SetMarks(std::span<const island::ItemMarks> marks) noexcept;

    // The hover label (docs/adr/0010): another NOREDIRECTIONBITMAP window, drawn by the same compositor.
    // False if it can't be drawn into (then there are no labels).
    bool AttachLabel(HWND label) noexcept;
    // Draws text in a small rounded box in style's colors at dpi and returns the box's size in pixels, to
    // size the label window by; nullopt if it couldn't. The text is GDI's, drawn into a bitmap.
    std::optional<SIZE> RenderLabel(std::wstring_view text, const island::IslandStyle& style,
                                    unsigned dpi) noexcept;
    // Lets go of the label's font, bitmap and surface (after a quiet while: rule 4).
    void ReleaseLabelResources() noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace harbor::adapters
