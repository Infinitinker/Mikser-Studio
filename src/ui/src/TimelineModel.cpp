#include "TimelineModel.h"

#include "mikserstudio/ui/ClipSelection.h"
#include "mikserstudio/ui/Theme.h"

#include <QFileInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString kindName(core::MediaKind kind)
{
    switch (kind) {
    case core::MediaKind::Audio:
        return u"audio"_s;
    case core::MediaKind::Image:
        return u"image"_s;
    case core::MediaKind::Video:
        break;
    }
    return u"video"_s;
}

QColor kindColor(core::MediaKind kind, const core::Track* track)
{
    const ThemeTokens& theme = darkTheme();
    // A video file placed on an audio track is shown as its audio.
    if (track && track->kind == core::TrackKind::Audio)
        return theme.clipAudio;
    switch (kind) {
    case core::MediaKind::Audio:
        return theme.clipAudio;
    case core::MediaKind::Image:
        return theme.clipImage;
    case core::MediaKind::Video:
        break;
    }
    return theme.clipVideo;
}

QStringList trackOrderOf(const core::ProjectModel& project)
{
    QStringList video;
    QStringList audio;
    for (const core::Track& track : project.tracks())
        (track.kind == core::TrackKind::Video ? video : audio).append(track.id);
    std::ranges::reverse(video);
    return video + audio;
}

bool isOff(const core::Track* track)
{
    return track && (track->kind == core::TrackKind::Video ? track->hidden : track->muted);
}

}  // namespace

TimelineModel::TimelineModel(const core::ProjectModel& project, const ClipSelection& selection, QObject* parent)
    : QAbstractListModel(parent)
    , m_project(project)
    , m_selection(selection)
{
    connect(&project, &core::ProjectModel::clipAdded, this, &TimelineModel::onClipAdded);
    connect(&project, &core::ProjectModel::clipRemoved, this, &TimelineModel::onClipRemoved);
    connect(&project, &core::ProjectModel::clipChanged, this, &TimelineModel::onClipChanged);
    connect(&project, &core::ProjectModel::projectReset, this, &TimelineModel::reset);
    connect(&project, &core::ProjectModel::trackAdded, this, &TimelineModel::updateTrackOrder);
    connect(&project, &core::ProjectModel::trackRemoved, this, &TimelineModel::updateTrackOrder);
    connect(&project, &core::ProjectModel::trackChanged, this, &TimelineModel::onTrackChanged);
    connect(&selection, &ClipSelection::changed, this, [this] {
        if (!m_clipIds.isEmpty())
            Q_EMIT dataChanged(index(0), index(static_cast<int>(m_clipIds.size()) - 1), {SelectedRole});
    });
    reset();
}

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_clipIds.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_clipIds.size())
        return {};
    const core::Clip* clip = m_project.findClip(m_clipIds.at(index.row()));
    if (!clip)
        return {};
    const core::MediaItem* media = m_project.findMedia(clip->mediaId);
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return media ? media->name : QFileInfo(clip->mediaId).fileName();
    case ClipIdRole:
        return clip->id;
    case MediaIdRole:
        return clip->mediaId;
    case TrackIdRole:
        return clip->trackId;
    case TrackRowRole:
        return rowOfTrack(clip->trackId);
    case PositionRole:
        return QVariant::fromValue<qint64>(clip->position);
    case LengthRole:
        return QVariant::fromValue<qint64>(clip->length());
    case InRole:
        return QVariant::fromValue<qint64>(clip->in);
    case OutRole:
        return QVariant::fromValue<qint64>(clip->out);
    case KindRole:
        return media ? kindName(media->info.kind) : u"video"_s;
    case ColorRole:
        return kindColor(media ? media->info.kind : core::MediaKind::Video, m_project.findTrack(clip->trackId));
    case SelectedRole:
        return m_selection.contains(clip->id);
    case GeneratedRole:
        return media && !media->provenance.isEmpty();
    case TrackOffRole:
        return isOff(m_project.findTrack(clip->trackId));
    default:
        return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {
        {ClipIdRole, "clipId"},       {MediaIdRole, "mediaId"},   {NameRole, "name"},       {TrackIdRole, "trackId"},
        {TrackRowRole, "trackRow"},   {PositionRole, "position"}, {LengthRole, "length"},   {InRole, "inPoint"},
        {OutRole, "outPoint"},        {KindRole, "kind"},         {ColorRole, "clipColor"}, {SelectedRole, "selected"},
        {GeneratedRole, "generated"}, {TrackOffRole, "trackOff"},
    };
}

int TimelineModel::rowOfTrack(const QString& trackId) const
{
    return static_cast<int>(m_trackOrder.indexOf(trackId));
}

QString TimelineModel::trackAtRow(int row) const
{
    return m_trackOrder.value(row);
}

QString TimelineModel::clipIdAt(int row) const
{
    return m_clipIds.value(row);
}

int TimelineModel::rowOfClip(const QString& clipId) const
{
    return static_cast<int>(m_clipIds.indexOf(clipId));
}

void TimelineModel::onClipAdded(const QString& id)
{
    if (m_clipIds.contains(id))
        return;
    const int row = static_cast<int>(m_clipIds.size());
    beginInsertRows({}, row, row);
    m_clipIds.append(id);
    endInsertRows();
}

void TimelineModel::onClipRemoved(const QString& id)
{
    const int row = rowOfClip(id);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_clipIds.removeAt(row);
    endRemoveRows();
}

void TimelineModel::onClipChanged(const QString& id)
{
    const int row = rowOfClip(id);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row));
}

void TimelineModel::onTrackChanged(const QString& id)
{
    for (int row = 0; row < m_clipIds.size(); ++row) {
        const core::Clip* clip = m_project.findClip(m_clipIds.at(row));
        if (clip && clip->trackId == id)
            Q_EMIT dataChanged(index(row), index(row), {TrackOffRole});
    }
}

void TimelineModel::reset()
{
    beginResetModel();
    m_clipIds.clear();
    for (const core::Clip& clip : m_project.clips())
        m_clipIds.append(clip.id);
    const QStringList order = trackOrderOf(m_project);
    const bool tracksMoved = order != m_trackOrder;
    m_trackOrder = order;
    endResetModel();
    if (tracksMoved)
        Q_EMIT tracksChanged();
}

void TimelineModel::updateTrackOrder()
{
    const QStringList order = trackOrderOf(m_project);
    if (order == m_trackOrder)
        return;
    m_trackOrder = order;
    Q_EMIT tracksChanged();
    // Rows below an added or removed track move.
    if (!m_clipIds.isEmpty())
        Q_EMIT dataChanged(index(0), index(static_cast<int>(m_clipIds.size()) - 1), {TrackRowRole});
}

}  // namespace mikserstudio::ui
