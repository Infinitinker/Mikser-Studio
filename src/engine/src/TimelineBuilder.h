#pragma once

#include "mikserstudio/core/Project.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>

namespace Mlt {
class Producer;
class Profile;
class Tractor;
}  // namespace Mlt

namespace mikserstudio::engine {

class MltRuntime;

struct BuiltTimeline {
    std::unique_ptr<Mlt::Tractor> tractor;
    QStringList problems;  // User-facing; clips that could not be opened play as gaps.
};

// Turns a ProjectModel into an MLT tractor:
//   track 0      black background, as long as the timeline (at least one frame)
//   track 1..n   one playlist per project track, in project order (V1 below V2)
// Every video track is composited over track 0 with the runtime's video compositor, planted in
// track order so upper tracks end up on top; every track's audio is summed with "mix".
//
// Media producers are cached per (media, track) and reused by later builds made with the same
// profile, so a rebuild does not reopen every file.
class TimelineBuilder {
public:
    TimelineBuilder(const MltRuntime& runtime, Mlt::Profile& profile);
    ~TimelineBuilder();
    TimelineBuilder(const TimelineBuilder&) = delete;
    TimelineBuilder& operator=(const TimelineBuilder&) = delete;

    [[nodiscard]] BuiltTimeline build(const core::ProjectModel& project);

private:
    std::shared_ptr<Mlt::Producer> producerFor(const core::MediaItem& media, const QString& trackId,
                                               core::FramePos neededLength, QStringList& problems);

    const MltRuntime& m_runtime;
    Mlt::Profile& m_profile;
    QHash<QString, std::shared_ptr<Mlt::Producer>> m_cache;
};

}  // namespace mikserstudio::engine
