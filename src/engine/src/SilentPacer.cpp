#include "SilentPacer.h"

namespace mikserstudio::engine {

namespace {

constexpr int kMaxLateFrames = 4;

}  // namespace

SilentPacer::SilentPacer(core::Rational frameRate)
    : m_period(std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(frameRate.num > 0 ? static_cast<double>(frameRate.den) / frameRate.num : 0.04)))
{
}

bool SilentPacer::waitUntilDue(double speed)
{
    std::unique_lock lock(m_mutex);
    const Clock::time_point now = Clock::now();
    if (!m_anchor || speed != m_speed) {
        m_anchor = now;
        m_framesSinceAnchor = 0;
        m_speed = speed;
    }
    const Clock::time_point due = *m_anchor + m_period * m_framesSinceAnchor;
    ++m_framesSinceAnchor;
    if (now > due + m_period * kMaxLateFrames) {
        m_anchor = now;
        m_framesSinceAnchor = 1;
        return !m_cancelled;
    }
    m_wake.wait_until(lock, due, [this] { return m_cancelled; });
    return !m_cancelled;
}

bool SilentPacer::waitForRefresh()
{
    std::unique_lock lock(m_mutex);
    m_wake.wait(lock, [this] { return m_refreshRequested || m_cancelled; });
    m_refreshRequested = false;
    m_anchor.reset();
    return !m_cancelled;
}

void SilentPacer::refresh()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_refreshRequested = true;
        m_anchor.reset();
    }
    m_wake.notify_all();
}

void SilentPacer::cancel()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_cancelled = true;
    }
    m_wake.notify_all();
}

void SilentPacer::reset()
{
    const std::scoped_lock lock(m_mutex);
    m_cancelled = false;
    m_refreshRequested = false;
    m_anchor.reset();
}

}  // namespace mikserstudio::engine
