// Building the timeline's MLT tractor from the project: playlists, gaps, compositing and audio mixing.
#include "TimelineBuilder.h"

#include "Logging.h"
#include "MltResource.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <Mlt.h>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

constexpr int kHideVideo = 1;
constexpr int kHideAudio = 2;

int toMlt(core::FramePos frames)
{
    return static_cast<int>(std::clamp<core::FramePos>(frames, 0, std::numeric_limits<int>::max()));
}

void plant(Mlt::Tractor& tractor, Mlt::Profile& profile, const QString& service, int track)
{
    Mlt::Transition transition(profile, service.toUtf8().constData());
    if (!transition.is_valid()) {
        qCWarning(lcPlayback).noquote() << "MLT transition" << service << "could not be created; track" << track
                                        << "is not mixed in";
        return;
    }
    transition.set("always_active", 1);
    if (service == u"mix"_s)
        transition.set("sum", 1);
    else
        transition.set("progressive", 1);
    tractor.plant_transition(transition, 0, track);
}

core::FramePos neededLength(const core::ProjectModel& project, const QString& mediaId, const QString& trackId)
{
    core::FramePos length = 0;
    for (const core::Clip& clip : project.clips()) {
        if (clip.mediaId == mediaId && clip.trackId == trackId)
            length = std::max(length, clip.out);
    }
    return length;
}

}  // namespace

TimelineBuilder::TimelineBuilder(const MltRuntime& runtime, Mlt::Profile& profile)
    : m_runtime(runtime)
    , m_profile(profile)
{
}

TimelineBuilder::~TimelineBuilder() = default;

BuiltTimeline TimelineBuilder::build(const core::ProjectModel& project)
{
    BuiltTimeline built;
    built.tractor = std::make_unique<Mlt::Tractor>(m_profile);
    Mlt::Tractor& tractor = *built.tractor;

    const int length = toMlt(std::max<core::FramePos>(1, project.duration()));
    Mlt::Producer background(m_profile, "color:#000000");
    background.set("length", length);
    background.set_in_and_out(0, length - 1);
    Mlt::Playlist backgroundTrack(m_profile);
    backgroundTrack.append(background, 0, length - 1);
    tractor.set_track(backgroundTrack, 0);

    QHash<QString, std::shared_ptr<Mlt::Producer>> used;
    int index = 1;
    for (const core::Track& track : project.tracks()) {
        Mlt::Playlist playlist(m_profile);
        playlist.set("id", track.id.toUtf8().constData());
        core::FramePos cursor = 0;
        for (const core::Clip& clip : project.clips()) {
            if (clip.trackId != track.id)
                continue;
            if (clip.position > cursor)
                playlist.blank(toMlt(clip.position - cursor - 1));
            const core::MediaItem* media = project.findMedia(clip.mediaId);
            auto producer = media ? producerFor(*media, track.id, neededLength(project, clip.mediaId, track.id),
                                                built.problems)
                                  : nullptr;
            if (producer) {
                used.insert(media->id + u'|' + track.id, producer);
                playlist.append(*producer, toMlt(clip.in), toMlt(clip.out - 1));
            } else {
                playlist.blank(toMlt(clip.length() - 1));
            }
            cursor = clip.end();
        }

        int hide = 0;
        if (track.kind == core::TrackKind::Audio || track.hidden)
            hide |= kHideVideo;
        if (track.muted)
            hide |= kHideAudio;
        playlist.set("hide", hide);
        tractor.set_track(playlist, index);
        if (track.kind == core::TrackKind::Video)
            plant(tractor, m_profile, m_runtime.videoCompositor(), index);
        plant(tractor, m_profile, u"mix"_s, index);
        ++index;
    }

    m_cache = std::move(used);
    built.problems.removeDuplicates();
    return built;
}

std::shared_ptr<Mlt::Producer> TimelineBuilder::producerFor(const core::MediaItem& media, const QString& trackId,
                                                            core::FramePos neededLength, QStringList& problems)
{
    const QString key = media.id + u'|' + trackId;
    std::shared_ptr<Mlt::Producer> producer = m_cache.value(key);
    if (!producer || QString::fromUtf8(producer->get("mikserstudio.path")) != media.path) {
        auto opened = openProducer(m_profile, media.path);
        if (!opened) {
            problems.append(u"\"%1\" plays as a gap: %2"_s.arg(media.name, opened.error()));
            qCWarning(lcPlayback).noquote() << "Timeline media" << media.path << "unavailable:" << opened.error();
            return nullptr;
        }
        producer = std::move(*opened);
        producer->set("mikserstudio.path", media.path.toUtf8().constData());
    }
    if (media.info.kind == core::MediaKind::Image && producer->get_length() < neededLength) {
        const int length = toMlt(neededLength);
        producer->set("length", length);
        producer->set_in_and_out(0, length - 1);
    }
    return producer;
}

}  // namespace mikserstudio::engine
