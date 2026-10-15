#pragma once

#include <chrono>
#include <deque>

#include "core/ipc/messages.h"

namespace harbor::taskbar
{

// Time as the caller measures it. The dock uses QueryUnbiasedInterruptTime, which stops while the computer
// sleeps, so a sleep never looks like a silent watchdog.
using Milliseconds = std::chrono::milliseconds;

struct GuardPolicyConfig
{
    // Pings needed on a new connection before the dock may hide: a link that keeps dropping never hides.
    int stablePings = 3;
    // Longer than this without a ping, and Harbor Shell's watchdog counts as gone.
    Milliseconds pingTimeout{5000};
    // More re-hides than this within fightWindow means Explorer keeps showing the taskbar: stop trying.
    int fightLimit = 10;
    Milliseconds fightWindow{10000};
    // A peek (docs/adr/0012) ends this long after Explorer slid the taskbar back out...
    Milliseconds peekAway{1000};
    // ...or this long after it began, if the taskbar never slid in.
    Milliseconds peekStart{3000};
};

struct GuardDecision
{
    bool hide = false;
    // Why a requested hide isn't happening; None when it is, or when nothing was requested.
    ipc::TaskbarBlock blocked = ipc::TaskbarBlock::None;

    bool operator==(const GuardDecision&) const = default;
};

// The dock's taskbar safety rules (CLAUDE.md rule 1, docs/adr/0007) as a pure state machine. The taskbar is
// hidden only while all of these hold:
//   1. Harbor Shell asked for it, on the current connection;
//   2. its watchdog is alive: connected, at least stablePings pings on this connection, the last one
//      within pingTimeout;
//   3. the panic hotkey is registered;
//   4. no panic or Explorer-fighting block is set (only a request to show the taskbar clears one);
//   5. a taskbar exists;
//   6. the dock isn't in a kill-on-close job;
//   7. the island is on screen (drawn, visible, not cloaked, inside one monitor; docs/adr/0011): without
//      it, the user would have neither a taskbar nor a dock.
// The caller adds its own vetoes (the process is crashing, the session is ending, the dock is exiting).
//
// While the taskbar is hidden, the user may peek at it (docs/adr/0012): Explorer shows it, and until the peek
// ends that is neither undone nor counted toward the circuit breaker. A peek only ever shows the taskbar.
class GuardPolicy
{
  public:
    explicit GuardPolicy(GuardPolicyConfig config = {});

    // setTaskbar from Harbor Shell. hidden = false also clears a panic or Explorer-fighting block.
    void OnRequest(bool hidden);
    void OnLinkUp();
    // Forgets the request too: whoever connects next must ask again.
    void OnLinkDown();
    void OnPing(Milliseconds now);
    void OnHotkeyRegistered(bool registered);
    // Blocks hiding until Harbor Shell asks to show the taskbar (which it does when it sees the block).
    void OnPanic();
    void OnKillOnCloseJob(bool inside);
    void OnTaskbarFound(bool found);
    // Whether the island is on screen. Off until the dock says so.
    void OnPresentable(bool presentable);
    // Explorer showed the hidden taskbar again and the dock hid it. True if that tripped the circuit
    // breaker, which blocks hiding like the panic hotkey does. Not counted while peeking.
    bool OnRehide(Milliseconds now);

    // The user asked to see the taskbar (the dock's tray button). True if a peek began (or began again):
    // only while the taskbar is to be hidden.
    bool OnPeek(Milliseconds now);
    // Whether Explorer shows the taskbar (slid in) now; checked regularly while peeking. True if this ended
    // the peek: the taskbar slid out peekAway ago, or never slid in within peekStart.
    bool OnTaskbarShowing(bool showing, Milliseconds now);
    // The taskbar was given back (any restore): no peek outlives it.
    void EndPeek();
    bool Peeking() const;

    GuardDecision Evaluate(Milliseconds now) const;

  private:
    const GuardPolicyConfig m_config;
    bool m_requested = false;
    bool m_linkUp = false;
    int m_pingsOnThisLink = 0;
    Milliseconds m_lastPing{0};
    bool m_hotkeyRegistered = false;
    bool m_killOnCloseJob = false;
    bool m_taskbarFound = true;
    bool m_presentable = false;
    ipc::TaskbarBlock m_latch = ipc::TaskbarBlock::None;
    std::deque<Milliseconds> m_rehides;
    bool m_peeking = false;
    Milliseconds m_peekSince{0};
    bool m_slidIn = false;
    Milliseconds m_lastShowing{0};
};

} // namespace harbor::taskbar
