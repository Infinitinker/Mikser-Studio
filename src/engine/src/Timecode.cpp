#include "mikserstudio/engine/Timecode.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

QString formatTimecode(core::FramePos frame, core::Rational frameRate)
{
    const auto fps = std::max<core::FramePos>(1, std::llround(frameRate.toDouble()));
    const core::FramePos total = std::max<core::FramePos>(0, frame);
    const core::FramePos frames = total % fps;
    const core::FramePos seconds = (total / fps) % 60;
    const core::FramePos minutes = (total / (fps * 60)) % 60;
    const core::FramePos hours = total / (fps * 3600);
    return u"%1:%2:%3:%4"_s.arg(hours, 2, 10, u'0')
        .arg(minutes, 2, 10, u'0')
        .arg(seconds, 2, 10, u'0')
        .arg(frames, 2, 10, u'0');
}

}  // namespace mikserstudio::engine
