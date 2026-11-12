#pragma once

#include "mikserstudio/core/Project.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace mikserstudio::engine {

// The clock of the silent output: MLT's "null" consumer renders as fast as it can, so its
// consumer-frame-show handler waits here until each frame is due (one frame period apart at any
// speed; the speed only changes how far the producer moves per frame) and, while paused, until
// the player asks for a refresh. Running late by more than a few frames re-anchors the clock
// instead of rushing to catch up. cancel() releases a waiting MLT thread so it can be stopped.
class SilentPacer {
public:
    explicit SilentPacer(core::Rational frameRate);

    // MLT thread. Both return false when canceled.
    bool waitUntilDue(double speed);
    bool waitForRefresh();

    // Any thread.
    void refresh();
    void cancel();
    void reset();  // Clears cancel/refresh state; call while no MLT thread is waiting.

private:
    using Clock = std::chrono::steady_clock;

    std::mutex m_mutex;
    std::condition_variable m_wake;
    Clock::duration m_period;
    std::optional<Clock::time_point> m_anchor;
    std::int64_t m_framesSinceAnchor = 0;
    double m_speed = 0.0;
    bool m_refreshRequested = false;
    bool m_canceled = false;
};

}  // namespace mikserstudio::engine
