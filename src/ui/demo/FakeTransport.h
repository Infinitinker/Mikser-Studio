#pragma once

#include "mikserstudio/core/Transport.h"

#include <QTimer>

namespace mikserstudio::ui::demo {

// A transport without media: a timer advances the position at the frame rate (times the shuttle
// speed) between 0 and the duration, so monitors and the timeline playhead move like the real
// engine's.
class FakeTransport final : public core::Transport {
    Q_OBJECT

public:
    explicit FakeTransport(core::Rational frameRate, QObject* parent = nullptr);

    [[nodiscard]] core::FramePos position() const override { return m_position; }
    [[nodiscard]] core::FramePos duration() const override { return m_duration; }
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] core::Rational frameRate() const override { return m_frameRate; }
    [[nodiscard]] double speed() const override { return m_state == State::Playing ? m_speed : 0.0; }

    void play() override;
    void pause() override;
    void seek(core::FramePos frame) override;
    void setSpeed(double speed) override;

    void setDuration(core::FramePos frames);
    void setFrameRate(core::Rational frameRate);  // E.g. when a project with another rate opens.

private:
    void tick();
    void updateInterval();
    void setState(State state);

    core::Rational m_frameRate;
    core::FramePos m_position = 0;
    core::FramePos m_duration = 0;
    State m_state = State::Paused;
    double m_speed = 1.0;
    double m_accumulator = 0.0;
    QTimer m_timer;
};

}  // namespace mikserstudio::ui::demo
