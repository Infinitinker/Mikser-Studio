#include "Timecode.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

core::FramePos framesPerSecond(core::Rational rate)
{
    const double fps = rate.toDouble();
    return fps > 0.0 ? std::max<core::FramePos>(1, std::llround(fps)) : 30;
}

}  // namespace

QString formatTimecode(core::FramePos frame, core::Rational rate)
{
    const core::FramePos fps = framesPerSecond(rate);
    const bool negative = frame < 0;
    const core::FramePos total = negative ? -frame : frame;
    const core::FramePos frames = total % fps;
    const core::FramePos seconds = (total / fps) % 60;
    const core::FramePos minutes = (total / fps / 60) % 60;
    const core::FramePos hours = total / fps / 3600;
    return u"%1%2:%3:%4:%5"_s.arg(negative ? u"-"_s : QString())
        .arg(hours, 2, 10, u'0')
        .arg(minutes, 2, 10, u'0')
        .arg(seconds, 2, 10, u'0')
        .arg(frames, 2, 10, u'0');
}

QString formatDuration(core::FramePos frames, core::Rational rate)
{
    const double fps = rate.toDouble() > 0.0 ? rate.toDouble() : 30.0;
    const auto totalSeconds = static_cast<core::FramePos>(
        std::llround(static_cast<double>(std::max<core::FramePos>(frames, 0)) / fps));
    const core::FramePos seconds = totalSeconds % 60;
    const core::FramePos minutes = (totalSeconds / 60) % 60;
    const core::FramePos hours = totalSeconds / 3600;
    if (hours > 0)
        return u"%1:%2:%3"_s.arg(hours).arg(minutes, 2, 10, u'0').arg(seconds, 2, 10, u'0');
    return u"%1:%2"_s.arg(minutes).arg(seconds, 2, 10, u'0');
}

}  // namespace mikserstudio::ui
