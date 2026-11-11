#pragma once

#include "mikserstudio/services/AiTypes.h"

#include <QJsonObject>
#include <QString>

#include <expected>

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::services {

// What ai.upscale_clip needs to know about the clip it upscales.
struct UpscaleSource {
    QString clipId;
    QString mediaId;
    QString mediaName;
    QString mediaPath;
    // The next video track above the clip's track; empty when the clip is on the top video
    // track, so the result needs a new one.
    QString targetTrackId;
};

// Checks that the clip exists and shows video.
[[nodiscard]] std::expected<UpscaleSource, QString> resolveUpscaleSource(const core::ProjectModel& project,
                                                                         const QString& clipId);

// Brings a finished video.upscale job into the project through the bus, as one undo step
// ("Place AI Result"): imports the output with the job provenance ("project.import_media"), adds
// a video track on top when the source clip is on the top one ("timeline.add_track") and adds
// the result with "timeline.add_clip" over the source clip (same position and source range) on
// the track above it. Returns {mediaId, clipId, trackId}; the error message says how far it got
// (an imported result stays in the media bin, and the same undo step removes it).
[[nodiscard]] std::expected<QJsonObject, QString> placeUpscaledVideo(core::CommandBus& bus, core::ProjectModel& project,
                                                                     const QString& sourceClipId, int scale,
                                                                     const AiJobInfo& job);

}  // namespace mikserstudio::services
