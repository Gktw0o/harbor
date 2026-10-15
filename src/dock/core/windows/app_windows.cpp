#include "core/windows/app_windows.h"

#include <algorithm>

namespace harbor::windows
{

std::string LowerAscii(std::string_view text)
{
    std::string lowered(text);
    for (auto& c : lowered)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lowered;
}

AppIdentity IdentityOf(std::string_view aumid, std::string_view exe, std::uint32_t processId)
{
    if (!aumid.empty())
    {
        return {AppIdentity::Kind::Aumid, LowerAscii(aumid)};
    }
    if (!exe.empty())
    {
        return {AppIdentity::Kind::Exe, LowerAscii(exe)};
    }
    return {AppIdentity::Kind::Process, std::to_string(processId)};
}

void AppWindows::Upsert(const TrackedWindow& window)
{
    const AppId target = IdFor(window.app);
    Window entry{.id = window.id, .minimized = window.minimized};
    if (const auto known = m_appOfWindow.find(window.id); known != m_appOfWindow.end())
    {
        if (known->second == target)
        {
            // Same app: only being minimized can have changed.
            auto& windows = FindApp(target)->windows;
            const auto it = std::find_if(windows.begin(), windows.end(),
                                         [&](const Window& w) { return w.id == window.id; });
            if (it->minimized != window.minimized)
            {
                it->minimized = window.minimized;
                ++m_revision;
            }
            return;
        }
        // Another app now: it moves, keeping its place and its last activation.
        RemoveFromApp(known->second, window.id, &entry);
        entry.minimized = window.minimized;
    }
    else
    {
        entry.seen = m_nextSeen++;
    }

    App* app = FindApp(target);
    if (app == nullptr)
    {
        m_apps.push_back({.id = target,
                          .identity = window.app,
                          .aumid = window.aumid,
                          .exe = window.exe,
                          .started = window.processStarted,
                          .created = m_nextSeen++});
        app = &m_apps.back();
    }
    auto& windows = app->windows;
    windows.insert(std::upper_bound(windows.begin(), windows.end(), entry,
                                    [](const Window& a, const Window& b) { return a.seen < b.seen; }),
                   entry);
    m_appOfWindow[window.id] = target;
    Sort();
    ++m_revision;
}

void AppWindows::Remove(WindowId id)
{
    const auto known = m_appOfWindow.find(id);
    if (known == m_appOfWindow.end())
    {
        return;
    }
    const AppId app = known->second;
    m_appOfWindow.erase(known);
    RemoveFromApp(app, id, nullptr);
    ++m_revision;
}

void AppWindows::Activated(WindowId id)
{
    const auto known = m_appOfWindow.find(id);
    if (known == m_appOfWindow.end())
    {
        return;
    }
    auto& windows = FindApp(known->second)->windows;
    std::find_if(windows.begin(), windows.end(), [&](const Window& w) { return w.id == id; })->active =
        m_nextActive++;
    ++m_revision;
}

std::optional<AppId> AppWindows::AppOf(WindowId id) const
{
    const auto known = m_appOfWindow.find(id);
    return known != m_appOfWindow.end() ? std::optional(known->second) : std::nullopt;
}

AppWindows::App* AppWindows::FindApp(AppId id)
{
    const auto it = std::find_if(m_apps.begin(), m_apps.end(), [&](const App& app) { return app.id == id; });
    return it != m_apps.end() ? &*it : nullptr;
}

AppId AppWindows::IdFor(const AppIdentity& identity)
{
    const auto [it, inserted] = m_ids.try_emplace(identity, m_nextId);
    if (inserted)
    {
        ++m_nextId;
    }
    return it->second;
}

void AppWindows::RemoveFromApp(AppId appId, WindowId id, Window* removed)
{
    const auto app = std::find_if(m_apps.begin(), m_apps.end(), [&](const App& a) { return a.id == appId; });
    auto& windows = app->windows;
    const auto it = std::find_if(windows.begin(), windows.end(), [&](const Window& w) { return w.id == id; });
    if (removed != nullptr)
    {
        *removed = *it;
    }
    windows.erase(it);
    if (windows.empty())
    {
        if (app->identity.kind == AppIdentity::Kind::Process)
        {
            m_ids.erase(app->identity);
        }
        m_apps.erase(app);
    }
}

void AppWindows::Sort()
{
    std::stable_sort(m_apps.begin(), m_apps.end(), [](const App& a, const App& b) {
        return a.started != b.started ? a.started < b.started : a.created < b.created;
    });
}

} // namespace harbor::windows
