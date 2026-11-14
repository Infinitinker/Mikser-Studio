// The project model: the rules for media, tracks and clips, and the changes that keep them consistent.
#include "mikserstudio/core/Project.h"

#include "Logging.h"
#include "ProjectJson.h"
#include "ProjectMigrations.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

bool fitsTrack(const MediaInfo& info, TrackKind track)
{
    if (track == TrackKind::Video)
        return info.kind != MediaKind::Audio;
    return info.kind == MediaKind::Audio || info.hasAudio;
}

// The rules for one media item that do not depend on the rest of the project.
std::optional<QString> mediaProblem(const MediaItem& item)
{
    if (item.id.isEmpty())
        return u"Media needs an id."_s;
    if (item.path.isEmpty())
        return u"Media “%1” needs a file path."_s.arg(item.id);
    if (item.info.durationFrames < 0)
        return u"Media “%1” has a negative duration."_s.arg(item.id);
    if (item.info.durationFrames > kMaxFrame) {
        return u"Media “%1” is %2 frames long; Mikser Studio supports at most %3 frames."_s.arg(item.id)
            .arg(item.info.durationFrames)
            .arg(kMaxFrame);
    }
    return std::nullopt;
}

// The placement rules except overlaps; `media` and `track` are the clip's references or null.
std::optional<QString> clipProblem(const Clip& clip, const MediaItem* media, const Track* track)
{
    if (!media)
        return u"There is no media with id “%1” in the project."_s.arg(clip.mediaId);
    if (!track)
        return u"There is no track “%1”."_s.arg(clip.trackId);
    if (!fitsTrack(media->info, track->kind)) {
        return track->kind == TrackKind::Video
                   ? u"“%1” has no picture, so it cannot go on video track %2."_s.arg(media->name, track->id)
                   : u"“%1” has no sound, so it cannot go on audio track %2."_s.arg(media->name, track->id);
    }
    if (clip.position < 0)
        return u"A clip cannot start before frame 0 (got %1)."_s.arg(clip.position);
    if (clip.in < 0 || clip.out <= clip.in)
        return u"Clip in/out points %1-%2 are invalid: in must be >= 0 and out greater than in."_s.arg(clip.in).arg(
            clip.out);
    if (clip.out > kMaxFrame)
        return u"Out point %1 is past frame %2, the last frame Mikser Studio supports."_s.arg(clip.out).arg(kMaxFrame);
    if (media->info.kind != MediaKind::Image && clip.out > media->info.durationFrames) {
        return u"Out point %1 is past the end of “%2”, which is %3 frames long."_s.arg(clip.out)
            .arg(media->name)
            .arg(media->info.durationFrames);
    }
    // in >= 0 and out <= kMaxFrame, so length() <= kMaxFrame and the subtraction cannot overflow.
    if (clip.position > kMaxFrame - clip.length()) {
        return u"A clip starting at frame %1 would end past frame %2, the last frame Mikser Studio supports."_s
            .arg(clip.position)
            .arg(kMaxFrame);
    }
    return std::nullopt;
}

QString overlapProblem(const Clip& other)
{
    return u"The clip would overlap clip “%1” on track %2 (frames %3-%4). Move it to a free spot."_s
        .arg(other.id, other.trackId)
        .arg(other.position)
        .arg(other.end());
}

}  // namespace

bool isAllowedMediaPath(const QString& path)
{
    static const QRegularExpression color(uR"(^color:(#[0-9A-Fa-f]{6}|#[0-9A-Fa-f]{8}|0x[0-9A-Fa-f]{8}|[A-Za-z]+)$)"_s);
    if (color.match(path).hasMatch())
        return true;
    const QString normalized = QDir::fromNativeSeparators(path);
    return !normalized.startsWith(u"//"_s) && QDir::isAbsolutePath(normalized);
}

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent)
{
    reset();
}

ProjectModel::~ProjectModel() = default;

const MediaItem* ProjectModel::findMedia(const QString& id) const
{
    const auto it = std::ranges::find(m_media, id, &MediaItem::id);
    return it == m_media.end() ? nullptr : &*it;
}

const Track* ProjectModel::findTrack(const QString& id) const
{
    const auto it = std::ranges::find(m_tracks, id, &Track::id);
    return it == m_tracks.end() ? nullptr : &*it;
}

const Clip* ProjectModel::findClip(const QString& id) const
{
    const auto it = std::ranges::find(m_clips, id, &Clip::id);
    return it == m_clips.end() ? nullptr : &*it;
}

FramePos ProjectModel::duration() const
{
    FramePos end = 0;
    for (const Clip& clip : m_clips)
        end = std::max(end, clip.end());
    return end;
}

std::expected<void, QString> ProjectModel::addMedia(MediaItem item, qsizetype index)
{
    if (const auto problem = mediaProblem(item))
        return std::unexpected(*problem);
    if (findMedia(item.id))
        return std::unexpected(u"Media id “%1” is already used in this project."_s.arg(item.id));
    if (index < -1 || index > m_media.size())
        return std::unexpected(u"Media bin position %1 is out of range (0 to %2)."_s.arg(index).arg(m_media.size()));
    if (item.name.isEmpty())
        item.name = QFileInfo(item.path).fileName();

    const QString id = item.id;
    m_media.insert(index < 0 ? m_media.size() : index, std::move(item));
    Q_EMIT mediaAdded(id);
    return {};
}

std::expected<MediaItem, QString> ProjectModel::removeMedia(const QString& id)
{
    const auto it = std::ranges::find(m_media, id, &MediaItem::id);
    if (it == m_media.end())
        return std::unexpected(u"There is no media with id “%1” in the project."_s.arg(id));
    const auto users = std::ranges::count(m_clips, id, &Clip::mediaId);
    if (users > 0) {
        return std::unexpected(
            u"“%1” is used by %2 clip(s) on the timeline. Remove those clips first."_s.arg(it->name).arg(users));
    }
    MediaItem removed = std::move(*it);
    m_media.erase(it);
    Q_EMIT mediaRemoved(id);
    return removed;
}

std::optional<QString> ProjectModel::checkPlacement(const Clip& clip, const QString& ignoreClipId) const
{
    if (auto problem = clipProblem(clip, findMedia(clip.mediaId), findTrack(clip.trackId)))
        return problem;
    for (const Clip& other : m_clips) {
        if (other.trackId != clip.trackId || other.id == ignoreClipId || other.id == clip.id)
            continue;
        if (clip.position < other.end() && other.position < clip.end())
            return overlapProblem(other);
    }
    return std::nullopt;
}

void ProjectModel::sortClips()
{
    QHash<QString, qsizetype> trackIndex;
    trackIndex.reserve(m_tracks.size());
    for (qsizetype i = 0; i < m_tracks.size(); ++i)
        trackIndex.insert(m_tracks[i].id, i);
    std::ranges::stable_sort(m_clips, [&trackIndex](const Clip& a, const Clip& b) {
        const qsizetype left = trackIndex.value(a.trackId, -1);
        const qsizetype right = trackIndex.value(b.trackId, -1);
        return left != right ? left < right : a.position < b.position;
    });
}

std::expected<void, QString> ProjectModel::addClip(Clip clip)
{
    if (clip.id.isEmpty())
        return std::unexpected(u"A clip needs an id."_s);
    if (findClip(clip.id))
        return std::unexpected(u"Clip id “%1” is already used on the timeline."_s.arg(clip.id));
    if (const auto problem = checkPlacement(clip, QString()))
        return std::unexpected(*problem);

    const QString id = clip.id;
    m_clips.append(std::move(clip));
    sortClips();
    Q_EMIT clipAdded(id);
    Q_EMIT timelineChanged();
    return {};
}

std::expected<Clip, QString> ProjectModel::removeClip(const QString& id)
{
    const auto it = std::ranges::find(m_clips, id, &Clip::id);
    if (it == m_clips.end())
        return std::unexpected(u"There is no clip with id “%1” on the timeline."_s.arg(id));
    Clip removed = std::move(*it);
    m_clips.erase(it);
    Q_EMIT clipRemoved(id);
    Q_EMIT timelineChanged();
    return removed;
}

std::expected<void, QString> ProjectModel::moveClip(const QString& id, const QString& trackId, FramePos position)
{
    const auto it = std::ranges::find(m_clips, id, &Clip::id);
    if (it == m_clips.end())
        return std::unexpected(u"There is no clip with id “%1” on the timeline."_s.arg(id));
    Clip moved = *it;
    moved.trackId = trackId;
    moved.position = position;
    if (const auto problem = checkPlacement(moved, id))
        return std::unexpected(*problem);

    *it = std::move(moved);
    sortClips();
    Q_EMIT clipChanged(id);
    Q_EMIT timelineChanged();
    return {};
}

std::expected<void, QString> ProjectModel::trimClip(const QString& id, FramePos in, FramePos out)
{
    const auto it = std::ranges::find(m_clips, id, &Clip::id);
    if (it == m_clips.end())
        return std::unexpected(u"There is no clip with id “%1” on the timeline."_s.arg(id));
    Clip trimmed = *it;
    trimmed.in = in;
    trimmed.out = out;
    if (const auto problem = checkPlacement(trimmed, id))
        return std::unexpected(*problem);

    *it = std::move(trimmed);
    Q_EMIT clipChanged(id);
    Q_EMIT timelineChanged();
    return {};
}

std::expected<QString, QString> ProjectModel::addTrack(TrackKind kind)
{
    const QChar letter = kind == TrackKind::Video ? u'V' : u'A';
    int highest = 0;
    for (const Track& track : std::as_const(m_tracks)) {
        if (!track.id.startsWith(letter))
            continue;
        bool isNumber = false;
        const int number = QStringView(track.id).sliced(1).toInt(&isNumber);
        if (isNumber)
            highest = std::max(highest, number);
    }
    if (highest == std::numeric_limits<int>::max())
        return std::unexpected(u"No more %1 tracks can be added: the track numbers are used up."_s.arg(toString(kind)));

    const int number = highest + 1;
    Track track{
        .id = letter + QString::number(number),
        .kind = kind,
        .name = (kind == TrackKind::Video ? u"Video %1"_s : u"Audio %1"_s).arg(number),
    };
    qsizetype index = m_tracks.size();
    if (kind == TrackKind::Video) {
        const auto lastVideo = std::ranges::find_last(m_tracks, TrackKind::Video, &Track::kind);
        index = lastVideo.empty() ? 0 : (lastVideo.begin() - m_tracks.begin()) + 1;
    }
    QString id = track.id;
    m_tracks.insert(index, std::move(track));
    Q_EMIT trackAdded(id);
    Q_EMIT timelineChanged();
    return id;
}

std::expected<Track, QString> ProjectModel::removeTrack(const QString& id)
{
    const auto it = std::ranges::find(m_tracks, id, &Track::id);
    if (it == m_tracks.end())
        return std::unexpected(u"There is no track “%1”."_s.arg(id));
    const auto clips = std::ranges::count(m_clips, id, &Clip::trackId);
    if (clips > 0)
        return std::unexpected(u"Track %1 still holds %2 clip(s). Remove those clips first."_s.arg(id).arg(clips));
    Track removed = std::move(*it);
    m_tracks.erase(it);
    Q_EMIT trackRemoved(id);
    Q_EMIT timelineChanged();
    return removed;
}

std::expected<void, QString> ProjectModel::setTrackFlags(const QString& id, bool muted, bool hidden)
{
    const auto it = std::ranges::find(m_tracks, id, &Track::id);
    if (it == m_tracks.end())
        return std::unexpected(u"There is no track “%1”."_s.arg(id));
    if (it->muted == muted && it->hidden == hidden)
        return {};
    it->muted = muted;
    it->hidden = hidden;
    Q_EMIT trackChanged(id);
    Q_EMIT timelineChanged();
    return {};
}

void ProjectModel::setFilePath(const QString& path)
{
    if (path == m_filePath)
        return;
    m_filePath = path;
    Q_EMIT filePathChanged(m_filePath);
}

void ProjectModel::reset(Rational frameRate, int width, int height)
{
    setFilePath({});
    m_frameRate = frameRate;
    m_width = width;
    m_height = height;
    m_media.clear();
    m_clips.clear();
    m_tracks = {
        Track{.id = u"V1"_s, .kind = TrackKind::Video, .name = u"Video 1"_s},
        Track{.id = u"V2"_s, .kind = TrackKind::Video, .name = u"Video 2"_s},
        Track{.id = u"A1"_s, .kind = TrackKind::Audio, .name = u"Audio 1"_s},
        Track{.id = u"A2"_s, .kind = TrackKind::Audio, .name = u"Audio 2"_s},
    };
    Q_EMIT projectReset();
    Q_EMIT timelineChanged();
}

QJsonObject ProjectModel::toJson() const
{
    QJsonArray media;
    for (const MediaItem& item : m_media)
        media.append(mediaToJson(item));
    QJsonArray tracks;
    for (const Track& track : m_tracks)
        tracks.append(trackToJson(track));
    QJsonArray clips;
    for (const Clip& clip : m_clips)
        clips.append(clipToJson(clip));
    return QJsonObject{
        {u"version"_s, kFormatVersion},
        {u"frameRate"_s, rationalToJson(m_frameRate)},
        {u"width"_s, m_width},
        {u"height"_s, m_height},
        {u"media"_s, media},
        {u"tracks"_s, tracks},
        {u"clips"_s, clips},
    };
}

std::expected<void, QString> ProjectModel::loadJson(const QJsonObject& json)
{
    auto migrated = migrateProject(json, kFormatVersion, projectMigrations());
    if (!migrated)
        return std::unexpected(migrated.error());
    auto document = projectFromJson(*migrated);
    if (!document)
        return std::unexpected(document.error());

    // Rebuild in a staging model so every rule is checked before anything here changes.
    ProjectModel staging;
    staging.m_frameRate = document->frameRate;
    staging.m_width = document->width;
    staging.m_height = document->height;
    staging.m_tracks.clear();
    QSet<QString> trackIds;
    for (Track& track : document->tracks) {
        if (trackIds.contains(track.id))
            return std::unexpected(u"The project file is damaged: track “%1” appears twice."_s.arg(track.id));
        trackIds.insert(track.id);
        staging.m_tracks.append(std::move(track));
    }
    // Lookups go through hashes and overlaps are checked once after sorting, so loading stays
    // O(n log n) for long timelines.
    staging.m_media.reserve(document->media.size());
    QSet<QString> mediaIds;
    for (MediaItem& item : document->media) {
        if (const auto problem = mediaProblem(item))
            return std::unexpected(u"The project file is damaged: %1"_s.arg(*problem));
        if (mediaIds.contains(item.id))
            return std::unexpected(u"The project file is damaged: media “%1” appears twice."_s.arg(item.id));
        mediaIds.insert(item.id);
        if (item.name.isEmpty())
            item.name = QFileInfo(item.path).fileName();
        staging.m_media.append(std::move(item));
    }
    QHash<QString, const MediaItem*> mediaById;
    for (const MediaItem& item : std::as_const(staging.m_media))
        mediaById.insert(item.id, &item);
    QHash<QString, const Track*> trackById;
    for (const Track& track : std::as_const(staging.m_tracks))
        trackById.insert(track.id, &track);

    staging.m_clips.reserve(document->clips.size());
    QSet<QString> clipIds;
    for (Clip& clip : document->clips) {
        if (clipIds.contains(clip.id))
            return std::unexpected(u"The project file is damaged: clip “%1” appears twice."_s.arg(clip.id));
        clipIds.insert(clip.id);
        if (const auto problem = clipProblem(clip, mediaById.value(clip.mediaId), trackById.value(clip.trackId)))
            return std::unexpected(u"The project file is damaged: clip “%1”: %2"_s.arg(clip.id, *problem));
        staging.m_clips.append(std::move(clip));
    }
    staging.sortClips();
    const QList<Clip>& sorted = staging.m_clips;
    for (qsizetype i = 1; i < sorted.size(); ++i) {
        const Clip& previous = sorted[i - 1];
        const Clip& current = sorted[i];
        // Sorted by start, so any overlap on a track shows up between neighbours.
        if (previous.trackId == current.trackId && current.position < previous.end()) {
            return std::unexpected(
                u"The project file is damaged: clip “%1”: %2"_s.arg(current.id, overlapProblem(previous)));
        }
    }

    m_frameRate = staging.m_frameRate;
    m_width = staging.m_width;
    m_height = staging.m_height;
    m_media = std::move(staging.m_media);
    m_tracks = std::move(staging.m_tracks);
    m_clips = std::move(staging.m_clips);
    qCInfo(lcProject) << "Loaded project with" << m_media.size() << "media items and" << m_clips.size() << "clips";
    Q_EMIT projectReset();
    Q_EMIT timelineChanged();
    return {};
}

QString ProjectModel::newId(const QString& prefix)
{
    return prefix + u'-' + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString toString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return u"video"_s;
    case MediaKind::Audio:
        return u"audio"_s;
    case MediaKind::Image:
        return u"image"_s;
    }
    return u"video"_s;
}

QString toString(TrackKind kind)
{
    return kind == TrackKind::Audio ? u"audio"_s : u"video"_s;
}

}  // namespace mikserstudio::core
