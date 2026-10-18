#pragma once

#include <chrono>
#include <optional>

#include "core/geometry/rect.h"

namespace harbor::appbar
{

using Milliseconds = std::chrono::milliseconds;

// The strip the dock wants to reserve: height pixels along the bottom of monitor. Height 0: registered only
// to hear Explorer's notifications (full screen), reserving nothing (docs/adr/0014).
struct Want
{
    geometry::Rect monitor;
    int height = 0;
    friend bool operator==(const Want&, const Want&) = default;
};

struct AppBarConfig
{
    // Explorer sends ABN_POSCHANGED in bursts (five right after registering): reposition at most this often.
    Milliseconds minInterval{250};
    // After a failed ABM_NEW (Explorer not running, say), try again this much later.
    Milliseconds registerRetry{2000};
};

enum class Action
{
    None,
    // ABM_NEW.
    Register,
    // ABM_QUERYPOS with Proposal(), Settle() the answer, then ABM_SETPOS if NeedsSet().
    Position,
    // ABM_REMOVE.
    Remove,
};

struct Decision
{
    Action action = Action::None;
    // With Action::None: when to ask again even if nothing else happens.
    std::optional<Milliseconds> wakeAt;
    friend bool operator==(const Decision&, const Decision&) = default;
};

// The dock's AppBar registration (SHAppBarMessage, bottom edge) as a pure state machine: the caller makes
// each call Next() asks for and reports the result. Positions are rate-limited, and ABM_SETPOS is only sent
// when the strip actually changes, so a burst of ABN_POSCHANGED costs a few queries and never feeds itself.
class AppBarState
{
  public:
    explicit AppBarState(AppBarConfig config = {});

    // nullopt: no AppBar (the dock is stopping).
    void SetWant(std::optional<Want> want);
    void OnRegistered(bool succeeded, Milliseconds now);
    void OnRemoved();
    // ABN_POSCHANGED, or a display or work-area change: the strip may have to move.
    void OnPositionMayHaveChanged();
    // Explorer (re)started, or the primary DPI changed (TaskbarCreated): register again, removing first in
    // case the old registration survived (ABM_NEW fails for a registered window).
    void OnTaskbarCreated();
    // After Action::Position: what was asked for with ABM_SETPOS (or would have been) and what Explorer
    // granted.
    void OnPositioned(const geometry::Rect& settled, const geometry::Rect& granted, Milliseconds now);

    Decision Next(Milliseconds now) const;

    // For Action::Position: the rectangle to query...
    geometry::Rect Proposal() const;
    // ...the answer brought back to our height, at the bottom Explorer allows (it moves the strip above a
    // visible taskbar)...
    geometry::Rect Settle(const geometry::Rect& queried) const;
    // ...and whether that needs an ABM_SETPOS: only when it differs from the last one.
    bool NeedsSet(const geometry::Rect& settled) const;

    // The strip Explorer last granted, while registered.
    std::optional<geometry::Rect> Granted() const
    {
        return m_granted;
    }

  private:
    const AppBarConfig m_config;
    std::optional<Want> m_want;
    bool m_registered = false;
    // Remove before the next Register.
    bool m_removeFirst = false;
    std::optional<Milliseconds> m_registerFailedAt;
    bool m_dirty = false;
    std::optional<Milliseconds> m_lastPositioned;
    std::optional<geometry::Rect> m_lastSettled;
    std::optional<geometry::Rect> m_granted;
};

// Makes the call state.Next() asks for through port and reports the result. Port has
// bool Register(), void Remove(), Rect Query(Rect) and Rect Set(Rect), each a SHAppBarMessage call in the
// dock. Returns the decision it acted on; Action::None means nothing to do until wakeAt or a new input.
template <typename Port> Decision RunStep(AppBarState& state, Port& port, Milliseconds now)
{
    const Decision decision = state.Next(now);
    switch (decision.action)
    {
    case Action::Register:
        state.OnRegistered(port.Register(), now);
        break;
    case Action::Remove:
        port.Remove();
        state.OnRemoved();
        break;
    case Action::Position: {
        const geometry::Rect settled = state.Settle(port.Query(state.Proposal()));
        const geometry::Rect granted = state.NeedsSet(settled) ? port.Set(settled) : *state.Granted();
        state.OnPositioned(settled, granted, now);
        break;
    }
    case Action::None:
        break;
    }
    return decision;
}

} // namespace harbor::appbar
