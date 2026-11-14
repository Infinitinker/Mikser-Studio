// The one-slot hand-over of frames from a player's MLT thread to the monitor window: it keeps only the newest frame.
#pragma once

#include "mikserstudio/engine/FrameSink.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>

namespace mikserstudio::engine {

// A one-slot, thread-safe hand-over from the MLT thread to the monitor: only the newest frame is
// kept, a frame replaced before it was taken counts as dropped. The listener is called (on the
// presenting thread, with the slot lock held so it cannot race with setListener) when a frame
// arrives and no earlier notification is still waiting for take().
class FrameMailbox final : public FrameSink {
public:
    void presentFrame(VideoFrame frame) override;

    [[nodiscard]] std::optional<VideoFrame> take();
    void setListener(std::function<void()> listener);

    [[nodiscard]] quint64 received() const { return m_received.load(); }
    [[nodiscard]] quint64 dropped() const { return m_dropped.load(); }

private:
    std::mutex m_mutex;
    std::optional<VideoFrame> m_pending;
    std::function<void()> m_listener;
    bool m_notified = false;
    std::atomic<quint64> m_received{0};
    std::atomic<quint64> m_dropped{0};
};

}  // namespace mikserstudio::engine
