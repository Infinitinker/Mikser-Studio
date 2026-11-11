#pragma once

#include "mikserstudio/core/Project.h"

namespace mikserstudio::ui::demo {

// Describes files by their extension only (no decoding): video files are 1920x1080 with stereo
// audio, audio files are stereo 48 kHz, images are stills. Durations come from the file name so
// the same name always gets the same length (8 to 30 seconds).
class FakeMediaProbe final : public core::MediaProbe {
public:
    [[nodiscard]] std::expected<core::MediaInfo, QString> probe(const QString& path,
                                                                core::Rational projectFrameRate) override;
};

}  // namespace mikserstudio::ui::demo
