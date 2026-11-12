#pragma once

#include "mikserstudio/core/Project.h"

namespace mikserstudio::engine {

class MltRuntime;

// core::MediaProbe on top of MLT. Accepts absolute local file paths and color resources such as
// "color:#ff0000"; nothing else reaches MLT (see openProducer). Durations are expressed in the requested project frame rate; still images
// (and color producers) are reported as MediaKind::Image with MLT's default length.
// Main thread only.
class MltMediaProbe final : public core::MediaProbe {
public:
    explicit MltMediaProbe(const MltRuntime& runtime);

    [[nodiscard]] std::expected<core::MediaInfo, QString> probe(const QString& path,
                                                                core::Rational projectFrameRate) override;

private:
    const MltRuntime& m_runtime;
};

}  // namespace mikserstudio::engine
