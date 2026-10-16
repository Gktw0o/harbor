#include "core/island/tuck.h"

namespace harbor::island
{

Tuck::Tuck(TuckConfig config) : m_config(config)
{
}

void Tuck::OnFullScreen(bool fullScreen, Milliseconds now)
{
    if (fullScreen == m_fullScreen)
    {
        return;
    }
    m_fullScreen = fullScreen;
    m_tucked = false;
    m_tuckAt.reset();
    if (fullScreen && !m_pointerOver)
    {
        m_tuckAt = now + m_config.delay;
    }
}

void Tuck::OnPointer(bool over, Milliseconds now)
{
    if (over == m_pointerOver)
    {
        return;
    }
    m_pointerOver = over;
    if (over)
    {
        m_tucked = false;
        m_tuckAt.reset();
    }
    else if (m_fullScreen)
    {
        m_tuckAt = now + m_config.afterLeave;
    }
}

void Tuck::OnMonitorChanged()
{
    m_fullScreen = false;
    m_tucked = false;
    m_tuckAt.reset();
}

void Tuck::OnTimer(Milliseconds now)
{
    if (m_tuckAt && now >= *m_tuckAt)
    {
        m_tuckAt.reset();
        m_tucked = m_fullScreen && !m_pointerOver;
    }
}

} // namespace harbor::island
