#include "UpscalePlacement.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"

#include <QDir>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

const core::Track* videoTrackAbove(const core::ProjectModel& project, const QString& trackId)
{
    const auto& tracks = project.tracks();
    auto it = std::ranges::find(tracks, trackId, &core::Track::id);
    if (it == tracks.end())
        return nullptr;
    // Tracks are listed bottom-up per kind (V1 under V2), so "above" is later in the list.
    const auto above = std::find_if(std::next(it), tracks.end(),
                                    [](const core::Track& track) { return track.kind == core::TrackKind::Video; });
    return above == tracks.end() ? nullptr : &*above;
}

QString firstVideoOutput(const AiJobInfo& job)
{
    const auto it = std::ranges::find(job.outputs, u"video"_s, &AiJobOutput::kind);
    return it == job.outputs.end() ? QString() : it->path;
}

}  // namespace

std::expected<UpscaleSource, QString> resolveUpscaleSource(const core::ProjectModel& project, const QString& clipId)
{
    const core::Clip* clip = project.findClip(clipId);
    if (!clip)
        return std::unexpected(u"There is no clip %1 on the timeline. Select a clip and try again."_s.arg(clipId));
    const core::MediaItem* media = project.findMedia(clip->mediaId);
    if (!media)
        return std::unexpected(u"The media of clip %1 is missing from the media bin."_s.arg(clipId));
    if (media->info.kind != core::MediaKind::Video || !media->info.hasVideo) {
        return std::unexpected(
            u"\"%1\" is not a video, so it cannot be upscaled. Choose a video clip."_s.arg(media->name));
    }
    const core::Track* target = videoTrackAbove(project, clip->trackId);
    return UpscaleSource{
        .clipId = clip->id,
        .mediaId = media->id,
        .mediaName = media->name,
        .mediaPath = media->path,
        .targetTrackId = target ? target->id : QString(),
    };
}

std::expected<QJsonObject, QString> placeUpscaledVideo(core::CommandBus& bus, core::ProjectModel& project,
                                                       const QString& sourceClipId, int scale, const AiJobInfo& job)
{
    const QString output = firstVideoOutput(job);
    if (output.isEmpty())
        return std::unexpected(u"The AI service finished the upscale but returned no video."_s);

    // The clip may have changed while the job ran: use its current state.
    const core::Clip* sourceClip = project.findClip(sourceClipId);
    const core::MediaItem* sourceMedia = sourceClip ? project.findMedia(sourceClip->mediaId) : nullptr;
    QJsonObject provenance = job.provenance;
    provenance.insert(u"sourceClipId"_s, sourceClipId);
    if (sourceMedia)
        provenance.insert(u"sourceMediaId"_s, sourceMedia->id);
    const QString name = sourceMedia ? u"%1 (upscaled x%2)"_s.arg(sourceMedia->name).arg(scale)
                                     : u"Upscaled x%1"_s.arg(scale);
    const auto source = resolveUpscaleSource(project, sourceClipId);

    // Everything below is one undo step, also when it stops half-way.
    const core::UndoGroup group(bus.undoStack(), u"Place AI Result"_s);
    auto imported = bus.execute(u"project.import_media"_s,
                                QJsonObject{{u"path"_s, output}, {u"name"_s, name}, {u"provenance"_s, provenance}});
    if (!imported) {
        return std::unexpected(u"The upscaled video was saved to \"%1\" but could not be imported: %2"_s.arg(
            QDir::toNativeSeparators(output), imported.error().message));
    }
    const QString mediaId = imported->value(u"mediaId").toString();
    const QString notPlaced = u"The upscaled video was imported into the media bin, but it was not placed on the timeline: "_s;
    if (!source)
        return std::unexpected(notPlaced + source.error());

    QString trackId = source->targetTrackId;
    if (trackId.isEmpty()) {
        auto track = bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"video"_s}});
        if (!track)
            return std::unexpected(notPlaced + track.error().message);
        trackId = track->value(u"trackId").toString();
    }

    const core::Clip clip = *project.findClip(sourceClipId);
    const core::MediaItem* upscaled = project.findMedia(mediaId);
    const core::FramePos length = upscaled ? upscaled->info.durationFrames : clip.out;
    const core::FramePos out = std::min(clip.out, length);
    const core::FramePos in = std::min(clip.in, out - 1);
    auto added = bus.execute(u"timeline.add_clip"_s, QJsonObject{
                                                         {u"mediaId"_s, mediaId},
                                                         {u"trackId"_s, trackId},
                                                         {u"position"_s, clip.position},
                                                         {u"in"_s, std::max<core::FramePos>(0, in)},
                                                         {u"out"_s, out},
                                                     });
    if (!added) {
        return std::unexpected(u"The upscaled video was imported into the media bin, but it was not placed on %1: %2"_s.arg(
            trackId, added.error().message));
    }
    return QJsonObject{
        {u"mediaId"_s, mediaId},
        {u"clipId"_s, added->value(u"clipId").toString()},
        {u"trackId"_s, trackId},
    };
}

}  // namespace mikserstudio::services
