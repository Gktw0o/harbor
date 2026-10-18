#include "core/island/full_screen.h"

namespace harbor::island
{
namespace
{

bool Covers(const Foreground& foreground, const geometry::Rect& monitor) noexcept
{
    return foreground.bounds.left <= monitor.left && foreground.bounds.top <= monitor.top &&
           foreground.bounds.right >= monitor.right && foreground.bounds.bottom >= monitor.bottom;
}

} // namespace

void FullScreenApp::Open(const geometry::Rect& monitor, const std::optional<Foreground>& foreground)
{
    m_open = true;
    m_window.reset();
    if (foreground && Covers(*foreground, monitor))
    {
        m_window = foreground->window;
    }
}

void FullScreenApp::Close()
{
    m_open = false;
    m_window.reset();
}

bool FullScreenApp::LookAgain(const geometry::Rect& monitor, const std::optional<Foreground>& foreground)
{
    if (!m_open || !foreground)
    {
        return m_open;
    }
    const bool covers = Covers(*foreground, monitor);
    if (covers && (!m_window || *m_window == foreground->window))
    {
        m_window = foreground->window;
        return true;
    }
    Close();
    return false;
}

} // namespace harbor::island
