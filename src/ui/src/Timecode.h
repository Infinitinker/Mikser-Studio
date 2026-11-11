#pragma once

#include "mikserstudio/core/Project.h"

#include <QString>

namespace mikserstudio::ui {

// "HH:MM:SS:FF" (non-drop-frame; fractional rates count frames at the rounded rate).
[[nodiscard]] QString formatTimecode(core::FramePos frame, core::Rational rate);
// "1:23" / "1:02:03" for durations in lists.
[[nodiscard]] QString formatDuration(core::FramePos frames, core::Rational rate);

}  // namespace mikserstudio::ui
