#include "ProjectCommandsPrivate.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

std::expected<void, QString> discardClip(std::expected<Clip, QString> removed)
{
    return removed ? std::expected<void, QString>{} : std::unexpected(removed.error());
}

CommandResult addClip(ProjectModel& project, const QJsonObject& params)
{
    const QString mediaId = params.value(u"mediaId").toString();
    const MediaItem* media = project.findMedia(mediaId);
    if (!media)
        return commandFailure(CommandErrorCode::Failed,
                              u"There is no media with id \"%1\" in the project."_s.arg(mediaId));

    const FramePos in = params.value(u"in").toInteger(0);
    const FramePos fullOut = media->info.kind == MediaKind::Image ? in + media->info.durationFrames
                                                                  : media->info.durationFrames;
    Clip clip{
        .id = ProjectModel::newId(u"clip"_s),
        .mediaId = mediaId,
        .trackId = params.value(u"trackId").toString(),
        .position = params.value(u"position").toInteger(),
        .in = in,
        .out = params.contains(u"out") ? params.value(u"out").toInteger() : fullOut,
    };
    if (auto added = project.addClip(clip); !added)
        return commandFailure(CommandErrorCode::Failed, added.error());

    return CommandOutcome{
        QJsonObject{{u"clipId"_s, clip.id}},
        makeEdit(
            u"Add Clip"_s, [&project, id = clip.id] { return discardClip(project.removeClip(id)); },
            [&project, clip] { return project.addClip(clip); }),
    };
}

CommandResult removeClip(ProjectModel& project, const QJsonObject& params)
{
    auto removed = project.removeClip(params.value(u"clipId").toString());
    if (!removed)
        return commandFailure(CommandErrorCode::Failed, removed.error());

    const Clip clip = *removed;
    return CommandOutcome{
        QJsonObject{},
        makeEdit(
            u"Remove Clip"_s, [&project, clip] { return project.addClip(clip); },
            [&project, id = clip.id] { return discardClip(project.removeClip(id)); }),
    };
}

CommandResult moveClip(ProjectModel& project, const QJsonObject& params)
{
    const QString id = params.value(u"clipId").toString();
    const Clip* clip = project.findClip(id);
    if (!clip)
        return commandFailure(CommandErrorCode::Failed, u"There is no clip with id \"%1\" on the timeline."_s.arg(id));

    const QString oldTrack = clip->trackId;
    const FramePos oldPosition = clip->position;
    const QString newTrack = params.value(u"trackId").toString();
    const FramePos newPosition = params.value(u"position").toInteger();
    if (auto moved = project.moveClip(id, newTrack, newPosition); !moved)
        return commandFailure(CommandErrorCode::Failed, moved.error());

    return CommandOutcome{
        QJsonObject{},
        makeEdit(
            u"Move Clip"_s,
            [&project, id, oldTrack, oldPosition] { return project.moveClip(id, oldTrack, oldPosition); },
            [&project, id, newTrack, newPosition] { return project.moveClip(id, newTrack, newPosition); }),
    };
}

CommandResult trimClip(ProjectModel& project, const QJsonObject& params)
{
    const QString id = params.value(u"clipId").toString();
    const Clip* clip = project.findClip(id);
    if (!clip)
        return commandFailure(CommandErrorCode::Failed, u"There is no clip with id \"%1\" on the timeline."_s.arg(id));

    const FramePos oldIn = clip->in;
    const FramePos oldOut = clip->out;
    const FramePos newIn = params.value(u"in").toInteger();
    const FramePos newOut = params.value(u"out").toInteger();
    if (auto trimmed = project.trimClip(id, newIn, newOut); !trimmed)
        return commandFailure(CommandErrorCode::Failed, trimmed.error());

    return CommandOutcome{
        QJsonObject{},
        makeEdit(
            u"Trim Clip"_s, [&project, id, oldIn, oldOut] { return project.trimClip(id, oldIn, oldOut); },
            [&project, id, newIn, newOut] { return project.trimClip(id, newIn, newOut); }),
    };
}

CommandResult addTrack(ProjectModel& project, const QJsonObject& params)
{
    const TrackKind kind = params.value(u"kind").toString() == u"audio" ? TrackKind::Audio : TrackKind::Video;
    auto added = project.addTrack(kind);
    if (!added)
        return commandFailure(CommandErrorCode::Failed, added.error());

    const QString id = *added;
    return CommandOutcome{
        QJsonObject{{u"trackId"_s, id}},
        makeEdit(
            kind == TrackKind::Video ? u"Add Video Track"_s : u"Add Audio Track"_s,
            [&project, id]() -> std::expected<void, QString> {
                auto removed = project.removeTrack(id);
                return removed ? std::expected<void, QString>{} : std::unexpected(removed.error());
            },
            [&project, id, kind]() -> std::expected<void, QString> {
                auto again = project.addTrack(kind);
                if (!again)
                    return std::unexpected(again.error());
                if (*again != id)
                    return std::unexpected(u"the track came back as %1 instead of %2"_s.arg(*again, id));
                return {};
            }),
    };
}

QString trackFlagsText(const Track& before, bool muted, bool hidden)
{
    if (before.hidden == hidden)
        return (muted ? u"Mute Track %1"_s : u"Unmute Track %1"_s).arg(before.id);
    if (before.muted == muted)
        return (hidden ? u"Hide Track %1"_s : u"Show Track %1"_s).arg(before.id);
    return u"Change Track %1"_s.arg(before.id);
}

CommandResult setTrackFlags(ProjectModel& project, const QJsonObject& params)
{
    const QString id = params.value(u"trackId").toString();
    const Track* track = project.findTrack(id);
    if (!track)
        return commandFailure(CommandErrorCode::Failed, u"There is no track \"%1\"."_s.arg(id));

    const bool oldMuted = track->muted;
    const bool oldHidden = track->hidden;
    const bool muted = params.value(u"muted").toBool(oldMuted);
    const bool hidden = params.value(u"hidden").toBool(oldHidden);
    if (muted == oldMuted && hidden == oldHidden)
        return CommandOutcome{QJsonObject{}, nullptr};
    const QString text = trackFlagsText(*track, muted, hidden);
    if (auto changed = project.setTrackFlags(id, muted, hidden); !changed)
        return commandFailure(CommandErrorCode::Failed, changed.error());

    return CommandOutcome{
        QJsonObject{},
        makeEdit(
            text, [&project, id, oldMuted, oldHidden] { return project.setTrackFlags(id, oldMuted, oldHidden); },
            [&project, id, muted, hidden] { return project.setTrackFlags(id, muted, hidden); }),
    };
}

CommandSpec timelineSpec(QString id, QString title, QString description, QJsonObject schema)
{
    return CommandSpec{
        .id = std::move(id),
        .title = std::move(title),
        .description = std::move(description),
        .category = u"Timeline"_s,
        .paramsSchema = std::move(schema),
        .shortcut = {},
        .undoable = true,
    };
}

}  // namespace

void registerTimelineCommands(CommandBus& bus, ProjectModel& project)
{
    const QJsonObject clipId = idParam(u"Id of the clip."_s);
    const QJsonObject trackId = idParam(u"Track id, e.g. \"V1\" or \"A1\"."_s);
    const QJsonObject position = frameParam(u"Timeline frame where the clip starts."_s);

    bus.registerCommand(timelineSpec(u"timeline.add_clip"_s, u"Add Clip to Timeline"_s,
                                     u"Places a media item on a track; in/out default to the whole media."_s,
                                     objectSchema(
                                         QJsonObject{
                                             {u"mediaId"_s, idParam(u"Id of the media item."_s)},
                                             {u"trackId"_s, trackId},
                                             {u"position"_s, position},
                                             {u"in"_s, frameParam(u"First source frame used."_s)},
                                             {u"out"_s, frameParam(u"Source frame after the last one used."_s)},
                                         },
                                         {u"mediaId"_s, u"trackId"_s, u"position"_s})),
                        [&project](const QJsonObject& params) { return addClip(project, params); });

    bus.registerCommand(timelineSpec(u"timeline.remove_clip"_s, u"Remove Clip"_s,
                                     u"Removes a clip from the timeline."_s,
                                     objectSchema(QJsonObject{{u"clipId"_s, clipId}}, {u"clipId"_s})),
                        [&project](const QJsonObject& params) { return removeClip(project, params); });

    bus.registerCommand(
        timelineSpec(
            u"timeline.move_clip"_s, u"Move Clip"_s,
            u"Moves a clip to another position or track without changing its length."_s,
            objectSchema(QJsonObject{{u"clipId"_s, clipId}, {u"trackId"_s, trackId}, {u"position"_s, position}},
                         {u"clipId"_s, u"trackId"_s, u"position"_s})),
        [&project](const QJsonObject& params) { return moveClip(project, params); });

    bus.registerCommand(timelineSpec(u"timeline.trim_clip"_s, u"Trim Clip"_s,
                                     u"Changes which part of the source a clip uses; its start position stays."_s,
                                     objectSchema(
                                         QJsonObject{
                                             {u"clipId"_s, clipId},
                                             {u"in"_s, frameParam(u"First source frame used."_s)},
                                             {u"out"_s, frameParam(u"Source frame after the last one used."_s)},
                                         },
                                         {u"clipId"_s, u"in"_s, u"out"_s})),
                        [&project](const QJsonObject& params) { return trimClip(project, params); });

    bus.registerCommand(
        timelineSpec(u"timeline.add_track"_s, u"Add Track"_s,
                     u"Adds an empty track; a new video track goes on top of the other video tracks."_s,
                     objectSchema(QJsonObject{{u"kind"_s, QJsonObject{{u"enum"_s, QJsonArray{u"video"_s, u"audio"_s}},
                                                                      {u"description"_s, u"Kind of track to add."_s}}}},
                                  {u"kind"_s})),
        [&project](const QJsonObject& params) { return addTrack(project, params); });

    const auto flag = [](const QString& description) {
        return QJsonObject{{u"type"_s, u"boolean"_s}, {u"description"_s, description}};
    };
    bus.registerCommand(
        timelineSpec(u"timeline.set_track_flags"_s, u"Set Track Mute/Hide"_s,
                     u"Mutes or unmutes a track's sound and hides or shows its picture; omitted flags stay."_s,
                     objectSchema(
                         QJsonObject{
                             {u"trackId"_s, trackId},
                             {u"muted"_s, flag(u"True silences the track."_s)},
                             {u"hidden"_s, flag(u"True hides the track's picture."_s)},
                         },
                         {u"trackId"_s})),
        [&project](const QJsonObject& params) { return setTrackFlags(project, params); });
}

}  // namespace mikserstudio::core
