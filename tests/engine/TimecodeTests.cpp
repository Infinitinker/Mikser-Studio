// Timecodes: frames are shown as HH:MM:SS:FF at the rounded frame rate.
#include "EngineTestSupport.h"

#include "mikserstudio/engine/Timecode.h"

using namespace Qt::StringLiterals;
using mikserstudio::core::Rational;
using mikserstudio::engine::formatTimecode;

TEST_CASE("Timecodes are HH:MM:SS:FF at the rounded frame rate", "[timecode]")
{
    CHECK(formatTimecode(0, Rational{30, 1}) == u"00:00:00:00"_s);
    CHECK(formatTimecode(29, Rational{30, 1}) == u"00:00:00:29"_s);
    CHECK(formatTimecode(30, Rational{30, 1}) == u"00:00:01:00"_s);
    CHECK(formatTimecode(mikserstudio::core::FramePos{25} * 3661 + 7, Rational{25, 1}) == u"01:01:01:07"_s);
    CHECK(formatTimecode(mikserstudio::core::FramePos{30} * 60, Rational{30000, 1001}) == u"00:01:00:00"_s);
    CHECK(formatTimecode(-5, Rational{30, 1}) == u"00:00:00:00"_s);
    CHECK(formatTimecode(100, Rational{0, 1}) == u"00:01:40:00"_s);  // Bad rate: one frame per second.
}
