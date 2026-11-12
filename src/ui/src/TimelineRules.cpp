// The track choice and end-of-track lookup that mirror the project's placement rules.
#include "TimelineRules.h"

#include <algorithm>

namespace mikserstudio::ui {

bool trackAccepts(const core::Track& track, const core::MediaItem& media)
{
    if (track.kind == core::TrackKind::Video)
        return media.info.kind != core::MediaKind::Audio;
    return media.info.hasAudio || media.info.kind == core::MediaKind::Audio;
}

QString firstTrackFor(const core::ProjectModel& project, const core::MediaItem& media)
{
    for (const core::Track& track : project.tracks()) {
        if (trackAccepts(track, media))
            return track.id;
    }
    return {};
}

core::FramePos trackEnd(const core::ProjectModel& project, const QString& trackId)
{
    core::FramePos end = 0;
    for (const core::Clip& clip : project.clips()) {
        if (clip.trackId == trackId)
            end = std::max(end, clip.end());
    }
    return end;
}

}  // namespace mikserstudio::ui
