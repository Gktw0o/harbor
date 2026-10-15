#include "app/window_tracker.h"

#include <algorithm>

namespace harbor::dock
{

WindowTracker::WindowTracker(windows::IWindowSystem& system, std::string classPrefix)
    : m_system(system), m_classPrefix(std::move(classPrefix))
{
}

void WindowTracker::Rescan(Milliseconds now)
{
    const auto windows = m_system.TopLevelWindows();
    // Gone while nobody was listening.
    std::vector<windows::WindowId> known;
    for (const auto& app : m_apps.Apps())
    {
        for (const auto& window : app.windows)
        {
            known.push_back(window.id);
        }
    }
    for (const auto id : known)
    {
        if (std::find(windows.begin(), windows.end(), id) == windows.end())
        {
            m_apps.Remove(id);
        }
    }
    // Back to front, so the window in front ends up the one activated last.
    for (auto it = windows.rbegin(); it != windows.rend(); ++it)
    {
        Consider(*it, now);
        m_apps.Activated(*it);
    }
}

void WindowTracker::OnEvents(const std::vector<windows::WindowEvent>& events, Milliseconds now)
{
    using Kind = windows::WindowEvent::Kind;
    for (const auto& event : events)
    {
        switch (event.kind)
        {
        case Kind::Changed:
            Consider(event.window, now);
            break;
        case Kind::Destroyed:
            m_apps.Remove(event.window);
            m_moving.erase(event.window);
            break;
        case Kind::Foreground: {
            // The app may have changed its AppUserModelID since: read it again.
            const auto root = m_system.RootOwner(event.window);
            Consider(root, now);
            m_apps.Activated(root);
            break;
        }
        case Kind::MoveSizeStart:
            m_moving.insert(event.window);
            break;
        case Kind::MoveSizeEnd:
            m_moving.erase(event.window);
            break;
        }
    }
}

void WindowTracker::OnTimer(Milliseconds now)
{
    std::vector<windows::WindowId> due;
    std::erase_if(m_rechecks, [&](const auto& recheck) {
        if (recheck.first > now)
        {
            return false;
        }
        due.push_back(recheck.second);
        return true;
    });
    for (const auto window : due)
    {
        if (m_apps.AppOf(window))
        {
            Consider(window, now);
        }
    }
}

std::optional<WindowTracker::Milliseconds> WindowTracker::NextTimer() const
{
    if (m_rechecks.empty())
    {
        return std::nullopt;
    }
    return std::min_element(m_rechecks.begin(), m_rechecks.end())->first;
}

void WindowTracker::Consider(windows::WindowId window, Milliseconds now)
{
    const auto facts = m_system.FactsOf(window);
    if (!facts || !facts->className.starts_with(m_classPrefix) || !windows::IsAppWindow(*facts))
    {
        m_apps.Remove(window);
        return;
    }
    const auto app = m_system.AppOf(window);
    if (!app)
    {
        m_apps.Remove(window);
        return;
    }
    const bool known = m_apps.AppOf(window).has_value();
    m_apps.Upsert({.id = window,
                   .app = windows::IdentityOf(app->aumid, app->exe, app->processId),
                   .aumid = app->aumid,
                   .exe = app->exe,
                   .processStarted = app->processStarted,
                   .minimized = m_system.IsMinimized(window)});
    if (!known)
    {
        for (const auto delay : kRecheckDelays)
        {
            m_rechecks.emplace_back(now + delay, window);
        }
    }
}

} // namespace harbor::dock
