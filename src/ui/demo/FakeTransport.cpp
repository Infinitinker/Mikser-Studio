#include "FakeTransport.h"

#include <algorithm>
#include <cmath>

namespace mikserstudio::ui::demo {

FakeTransport::FakeTransport(core::Rational frameRate, QObject* parent)
    : core::Transport(parent)
    , m_frameRate(frameRate)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    updateInterval();
    connect(&m_timer, &QTimer::timeout, this, &FakeTransport::tick);
}

void FakeTransport::play()
{
    if (m_duration <= 0)
        return;
    if (m_speed == 0.0)
        m_speed = 1.0;
    if (m_position >= m_duration - 1 && m_speed > 0)
        seek(0);
    m_timer.start();
    setState(State::Playing);
}

void FakeTransport::pause()
{
    m_timer.stop();
    m_speed = 1.0;
    setState(State::Paused);
}

void FakeTransport::seek(core::FramePos frame)
{
    const core::FramePos clamped = std::clamp<core::FramePos>(frame, 0, std::max<core::FramePos>(0, m_duration - 1));
    m_accumulator = 0.0;
    if (clamped == m_position)
        return;
    m_position = clamped;
    Q_EMIT positionChanged(m_position);
}

void FakeTransport::setSpeed(double speed)
{
    if (speed == 0.0) {
        pause();
        return;
    }
    m_speed = speed;
    play();
}

void FakeTransport::setDuration(core::FramePos frames)
{
    if (frames == m_duration)
        return;
    m_duration = std::max<core::FramePos>(0, frames);
    Q_EMIT durationChanged(m_duration);
    if (m_position >= m_duration)
        seek(m_duration - 1);
}

void FakeTransport::setFrameRate(core::Rational frameRate)
{
    if (frameRate == m_frameRate)
        return;
    m_frameRate = frameRate;
    updateInterval();
    Q_EMIT frameRateChanged();
}

void FakeTransport::updateInterval()
{
    const double fps = std::max(1.0, m_frameRate.toDouble());
    m_timer.setInterval(static_cast<int>(std::lround(1000.0 / fps)));
}

void FakeTransport::tick()
{
    m_accumulator += m_speed;
    const auto step = static_cast<core::FramePos>(m_accumulator);
    if (step == 0)
        return;
    m_accumulator -= static_cast<double>(step);
    const core::FramePos next = m_position + step;
    if (next < 0 || next >= m_duration) {
        seek(next < 0 ? 0 : m_duration - 1);
        pause();
        return;
    }
    m_position = next;
    Q_EMIT positionChanged(m_position);
}

void FakeTransport::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

}  // namespace mikserstudio::ui::demo
