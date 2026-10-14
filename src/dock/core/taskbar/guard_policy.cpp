#include "core/taskbar/guard_policy.h"

namespace harbor::taskbar
{

using ipc::TaskbarBlock;

GuardPolicy::GuardPolicy(GuardPolicyConfig config) : m_config(config)
{
}

void GuardPolicy::OnRequest(bool hidden)
{
    m_requested = hidden;
    if (!hidden)
    {
        m_latch = TaskbarBlock::None;
        m_rehides.clear();
    }
}

void GuardPolicy::OnLinkUp()
{
    m_linkUp = true;
    m_pingsOnThisLink = 0;
}

void GuardPolicy::OnLinkDown()
{
    m_linkUp = false;
    m_pingsOnThisLink = 0;
    m_requested = false;
}

void GuardPolicy::OnPing(Milliseconds now)
{
    if (m_linkUp)
    {
        ++m_pingsOnThisLink;
        m_lastPing = now;
    }
}

void GuardPolicy::OnHotkeyRegistered(bool registered)
{
    m_hotkeyRegistered = registered;
}

void GuardPolicy::OnPanic()
{
    // A panic with nothing requested has nothing to block.
    if (m_requested && m_latch == TaskbarBlock::None)
    {
        m_latch = TaskbarBlock::PanicHotkey;
    }
}

void GuardPolicy::OnKillOnCloseJob(bool inside)
{
    m_killOnCloseJob = inside;
}

void GuardPolicy::OnTaskbarFound(bool found)
{
    m_taskbarFound = found;
}

bool GuardPolicy::OnRehide(Milliseconds now)
{
    while (!m_rehides.empty() && now - m_rehides.front() > m_config.fightWindow)
    {
        m_rehides.pop_front();
    }
    m_rehides.push_back(now);
    if (static_cast<int>(m_rehides.size()) <= m_config.fightLimit || m_latch != TaskbarBlock::None)
    {
        return false;
    }
    m_latch = TaskbarBlock::ExplorerFighting;
    return true;
}

GuardDecision GuardPolicy::Evaluate(Milliseconds now) const
{
    if (!m_requested)
    {
        return {};
    }
    // Blocks the user can act on, most specific first.
    if (m_latch != TaskbarBlock::None)
    {
        return {false, m_latch};
    }
    if (m_killOnCloseJob)
    {
        return {false, TaskbarBlock::KillOnCloseJob};
    }
    if (!m_hotkeyRegistered)
    {
        return {false, TaskbarBlock::HotkeyUnavailable};
    }
    if (!m_taskbarFound)
    {
        return {false, TaskbarBlock::TaskbarNotFound};
    }
    // The watchdog isn't proven alive (yet, or any more): not hiding, but nothing the user must fix.
    if (!m_linkUp || m_pingsOnThisLink < m_config.stablePings || now - m_lastPing > m_config.pingTimeout)
    {
        return {};
    }
    return {true, TaskbarBlock::None};
}

} // namespace harbor::taskbar
