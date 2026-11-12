// The seam between a player and a monitor: a decoded video frame, and the sink that receives the frames a player shows.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QImage>

namespace mikserstudio::engine {

// One decoded, composited video frame ready for display.
struct VideoFrame {
    QImage image;  // QImage::Format_RGBA8888, the player's output size.
    core::FramePos position = 0;  // Frame number in the player's frame rate.
};

// Receives the frames a player shows. presentFrame() is called on an MLT thread at display time
// (the audio clock decides when); implementations must be thread-safe and return quickly.
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void presentFrame(VideoFrame frame) = 0;
};

}  // namespace mikserstudio::engine
