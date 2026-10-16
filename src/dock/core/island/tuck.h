#pragma once

#include <chrono>
#include <optional>

namespace harbor::island
{

using Milliseconds = std::chrono::milliseconds;

struct TuckConfig
{
    // A full-screen app came to the dock's monitor: the island stands aside this much later.
    Milliseconds delay{2500};
    // Brought back over the full-screen app and the mouse left it: tucked again this much later.
    Milliseconds afterLeave{1000};
};

// Whether the dock's island is tucked behind a full-screen app on its monitor (docs/adr/0014), as a pure
// state machine. Tucked means behind the app (HWND_BOTTOM), still shown: the caller keeps the monitor's edge
// trigger armed meanwhile. The mouse on the island or resting on that trigger brings it back; the end of full
// screen always does, whoever notices it (Explorer, or the dock's own foreground check).
class Tuck
{
  public:
    explicit Tuck(TuckConfig config = {});

    // Full screen on the dock's monitor began or ended.
    void OnFullScreen(bool fullScreen, Milliseconds now);
    // The mouse is on the island (or a menu of it is open), or rests on its monitor's trigger: true; left it:
    // false.
    void OnPointer(bool over, Milliseconds now);
    // The dock moved to another monitor: what was full screen on the old one doesn't apply there.
    void OnMonitorChanged();
    // The timer the caller set for WakeAt() fired (or any time later).
    void OnTimer(Milliseconds now);

    bool Tucked() const noexcept
    {
        return m_tucked;
    }
    bool FullScreen() const noexcept
    {
        return m_fullScreen;
    }
    // When to call OnTimer next; nullopt: no timer needed.
    std::optional<Milliseconds> WakeAt() const noexcept
    {
        return m_tuckAt;
    }

  private:
    const TuckConfig m_config;
    bool m_fullScreen = false;
    bool m_pointerOver = false;
    bool m_tucked = false;
    std::optional<Milliseconds> m_tuckAt;
};

} // namespace harbor::island
