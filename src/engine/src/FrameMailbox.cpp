// The one-slot frame hand-over: storing the newest frame, counting dropped ones, notifying the monitor.
#include "FrameMailbox.h"

namespace mikserstudio::engine {

void FrameMailbox::presentFrame(VideoFrame frame)
{
    const std::scoped_lock lock(m_mutex);
    if (m_pending)
        ++m_dropped;
    m_pending = std::move(frame);
    ++m_received;
    if (!m_notified && m_listener) {
        m_notified = true;
        m_listener();
    }
}

std::optional<VideoFrame> FrameMailbox::take()
{
    const std::scoped_lock lock(m_mutex);
    m_notified = false;
    return std::exchange(m_pending, std::nullopt);
}

void FrameMailbox::setListener(std::function<void()> listener)
{
    const std::scoped_lock lock(m_mutex);
    m_listener = std::move(listener);
    m_notified = false;
}

}  // namespace mikserstudio::engine
