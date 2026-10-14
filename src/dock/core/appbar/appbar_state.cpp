#include "core/appbar/appbar_state.h"

namespace harbor::appbar
{

AppBarState::AppBarState(AppBarConfig config) : m_config(config)
{
}

void AppBarState::SetWant(std::optional<Want> want)
{
    if (want != m_want)
    {
        m_want = want;
        m_dirty = true;
    }
}

void AppBarState::OnRegistered(bool succeeded, Milliseconds now)
{
    m_registered = succeeded;
    m_dirty = succeeded;
    m_registerFailedAt = succeeded ? std::nullopt : std::optional(now);
    // A failure may mean the window is still registered from before: clear that before trying again.
    m_removeFirst = !succeeded;
}

void AppBarState::OnRemoved()
{
    m_registered = false;
    m_removeFirst = false;
    m_lastSettled.reset();
    m_granted.reset();
}

void AppBarState::OnPositionMayHaveChanged()
{
    // Unregistered, there's no strip to move; registering positions it anyway.
    if (m_registered)
    {
        m_dirty = true;
    }
}

void AppBarState::OnTaskbarCreated()
{
    m_registered = false;
    m_removeFirst = true;
    m_registerFailedAt.reset();
    m_lastSettled.reset();
    m_granted.reset();
}

void AppBarState::OnPositioned(const geometry::Rect& settled, const geometry::Rect& granted, Milliseconds now)
{
    m_lastSettled = settled;
    m_granted = granted;
    m_lastPositioned = now;
    m_dirty = false;
}

Decision AppBarState::Next(Milliseconds now) const
{
    if (!m_want)
    {
        return {(m_registered || m_removeFirst) ? Action::Remove : Action::None};
    }
    if (!m_registered)
    {
        if (m_registerFailedAt && now < *m_registerFailedAt + m_config.registerRetry)
        {
            return {Action::None, *m_registerFailedAt + m_config.registerRetry};
        }
        return {m_removeFirst ? Action::Remove : Action::Register};
    }
    if (m_dirty)
    {
        if (m_lastPositioned && now < *m_lastPositioned + m_config.minInterval)
        {
            return {Action::None, *m_lastPositioned + m_config.minInterval};
        }
        return {Action::Position};
    }
    return {};
}

geometry::Rect AppBarState::Proposal() const
{
    const auto& monitor = m_want->monitor;
    return {monitor.left, monitor.bottom - m_want->height, monitor.right, monitor.bottom};
}

geometry::Rect AppBarState::Settle(const geometry::Rect& queried) const
{
    return {queried.left, queried.bottom - m_want->height, queried.right, queried.bottom};
}

bool AppBarState::NeedsSet(const geometry::Rect& settled) const
{
    return !m_granted || settled != m_lastSettled;
}

} // namespace harbor::appbar
