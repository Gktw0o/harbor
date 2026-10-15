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
//   7. the island is on screen (drawn and visible on the primary monitor): without it, the user would have
//      neither a taskbar nor a dock.
// The caller adds its own vetoes (the process is crashing, the session is ending, the dock is exiting).
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
    // breaker, which blocks hiding like the panic hotkey does.
    bool OnRehide(Milliseconds now);

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
};

} // namespace harbor::taskbar
