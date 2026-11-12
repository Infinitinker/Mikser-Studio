// The demo project's media items and clips, placed through the project and timeline commands.
#include "DemoProject.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui::demo {

namespace {

struct DemoClip {
    QString file;
    QString trackId;
    double startSeconds;  // < 0: right after the previous clip on the same track.
    double lengthSeconds;
};

}  // namespace

std::expected<QStringList, QString> populateDemoProject(core::CommandBus& bus, const core::ProjectModel& project,
                                                        const QString& mediaDirectory)
{
    const QList<DemoClip> clips{
        {u"Interview A.mp4"_s, u"V1"_s, 0.0, 9.0},    {u"City at Dusk.mov"_s, u"V1"_s, -1.0, 7.0},
        {u"Market B-Roll.mp4"_s, u"V1"_s, -1.0, 8.0}, {u"Interview B.mp4"_s, u"V1"_s, -1.0, 10.0},
        {u"Title Card.png"_s, u"V2"_s, 1.0, 4.0},     {u"Drone Coast.mp4"_s, u"V2"_s, 17.0, 6.0},
        {u"Lower Third.png"_s, u"V3"_s, 3.0, 4.0},    {u"Score - Calm Pulse.wav"_s, u"A1"_s, 0.0, 26.0},
        {u"Room Tone.wav"_s, u"A2"_s, 2.0, 12.0},     {u"Street Ambience.wav"_s, u"A2"_s, 16.0, 14.0},
    };
    if (!QDir().mkpath(mediaDirectory))
        return std::unexpected(u"Could not create the demo media folder %1."_s.arg(mediaDirectory));
    if (const auto added = bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"video"_s}}); !added)
        return std::unexpected(added.error().message);

    const double fps = project.frameRate().toDouble();
    QHash<QString, core::FramePos> trackEnds;
    QHash<QString, QString> mediaIds;
    QStringList clipIds;
    for (const DemoClip& clip : clips) {
        const QString path = QDir(mediaDirectory).filePath(clip.file);
        if (!QFile::exists(path)) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly))
                return std::unexpected(u"Could not create the demo file %1: %2"_s.arg(path, file.errorString()));
        }
        if (!mediaIds.contains(clip.file)) {
            const auto imported = bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, path}});
            if (!imported)
                return std::unexpected(imported.error().message);
            mediaIds.insert(clip.file, imported->value(u"mediaId").toString());
        }
        const QString mediaId = mediaIds.value(clip.file);
        const core::MediaItem* media = project.findMedia(mediaId);
        const auto wanted = static_cast<core::FramePos>(clip.lengthSeconds * fps);
        const core::FramePos length = media && media->info.kind != core::MediaKind::Image
                                          ? std::min(wanted, media->info.durationFrames)
                                          : wanted;
        const core::FramePos position = clip.startSeconds < 0 ? trackEnds.value(clip.trackId)
                                                              : static_cast<core::FramePos>(clip.startSeconds * fps);
        const auto added = bus.execute(u"timeline.add_clip"_s, QJsonObject{{u"mediaId"_s, mediaId},
                                                                           {u"trackId"_s, clip.trackId},
                                                                           {u"position"_s, position},
                                                                           {u"in"_s, 0},
                                                                           {u"out"_s, length}});
        if (!added)
            return std::unexpected(added.error().message);
        clipIds.append(added->value(u"clipId").toString());
        trackEnds.insert(clip.trackId, position + length);
    }
    const auto muted = bus.execute(u"timeline.set_track_flags"_s,
                                   QJsonObject{{u"trackId"_s, u"A2"_s}, {u"muted"_s, true}});
    if (!muted)
        return std::unexpected(muted.error().message);
    return clipIds;
}

}  // namespace mikserstudio::ui::demo
