// Timecode text for a frame number, shown by the monitors' transport bars.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QString>

namespace mikserstudio::engine {

// "HH:MM:SS:FF" (non-drop-frame) for a frame number; the frames field counts up to the rounded
// frame rate (30 for 29.97). Negative frames are shown as 00:00:00:00.
[[nodiscard]] QString formatTimecode(core::FramePos frame, core::Rational frameRate);

}  // namespace mikserstudio::engine
