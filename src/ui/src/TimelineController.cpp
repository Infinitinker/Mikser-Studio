// The timeline controller: geometry and snapping, and the move, add, remove and seek commands.
#include "TimelineController.h"

#include "CommandInvoker.h"
#include "QuietCommands.h"
#include "Timecode.h"
#include "TimelineModel.h"
#include "TimelineRules.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/ClipSelection.h"

#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

const auto kSeekCommand = u"program.seek"_s;
constexpr double kMinRulerSpacing = 90.0;  // Pixels between labelled ruler ticks.
constexpr qint64 kTailSeconds = 10;  // Room to scroll past the last clip.

}  // namespace

TimelineController::TimelineController(core::CommandBus& bus, const core::ProjectModel& project,
                                       ClipSelection& selection, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_project(project)
    , m_selection(selection)
    , m_model(new TimelineModel(project, selection, this))
{
    connect(m_model, &TimelineModel::tracksChanged, this, [this] {
        Q_EMIT tracksChanged();
        setScrollY(m_scrollY);
    });
    connect(&project, &core::ProjectModel::trackChanged, this, &TimelineController::tracksChanged);
    connect(&project, &core::ProjectModel::timelineChanged, this, [this] {
        Q_EMIT durationChanged();
        // The content may have become shorter than the scrolled-to place.
        setScrollX(m_scrollX);
    });
    connect(&project, &core::ProjectModel::projectReset, this, [this] {
        Q_EMIT frameRateChanged();
        Q_EMIT zoomChanged();
        Q_EMIT durationChanged();
        setScrollX(0.0);
        setScrollY(0.0);
    });
    const auto seekAvailability = [this](const QString& id) {
        if (id == kSeekCommand)
            Q_EMIT canSeekChanged();
    };
    connect(&bus, &core::CommandBus::commandRegistered, this, seekAvailability);
    connect(&bus, &core::CommandBus::commandUnregistered, this, seekAvailability);
}

QObject* TimelineController::modelObject() const
{
    return m_model;
}

QVariantList TimelineController::tracks() const
{
    QVariantList list;
    for (const QString& id : m_model->trackOrder()) {
        const core::Track* track = m_project.findTrack(id);
        if (!track)
            continue;
        list.append(QVariantMap{
            {u"id"_s, track->id},
            {u"name"_s, track->name.isEmpty() ? track->id : track->name},
            {u"kind"_s, track->kind == core::TrackKind::Video ? u"video"_s : u"audio"_s},
            {u"muted"_s, track->muted},
            {u"hidden"_s, track->hidden},
        });
    }
    return list;
}

qint64 TimelineController::duration() const
{
    return m_project.duration();
}

qint64 TimelineController::rulerStep() const
{
    const qint64 fps = std::max<qint64>(1, std::llround(frameRate()));
    const qint64 seconds[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
    for (const qint64 frames : {qint64{1}, qint64{2}, qint64{5}, qint64{10}}) {
        if (frames < fps && static_cast<double>(frames) * m_pixelsPerFrame >= kMinRulerSpacing)
            return frames;
    }
    for (const qint64 second : seconds) {
        if (static_cast<double>(second * fps) * m_pixelsPerFrame >= kMinRulerSpacing)
            return second * fps;
    }
    return 3600 * fps;
}

void TimelineController::setViewportWidth(double width)
{
    if (width <= 0.0 || qFuzzyCompare(width, m_viewportWidth))
        return;
    m_viewportWidth = width;
    Q_EMIT viewportWidthChanged();
    setScrollX(m_scrollX);
}

void TimelineController::setViewportHeight(double height)
{
    if (height <= 0.0 || qFuzzyCompare(height, m_viewportHeight))
        return;
    m_viewportHeight = height;
    Q_EMIT viewportHeightChanged();
    setScrollY(m_scrollY);
}

bool TimelineController::canSeek() const
{
    return m_bus.contains(kSeekCommand);
}

void TimelineController::setSnapping(bool on)
{
    if (on == m_snapping)
        return;
    m_snapping = on;
    Q_EMIT snappingChanged();
}

void TimelineController::toggleSnapping()
{
    setSnapping(!m_snapping);
    Q_EMIT toastRequested(m_snapping ? u"Snapping on"_s : u"Snapping off. Press S to turn it back on."_s, false);
}

void TimelineController::setTransport(core::Transport* transport)
{
    disconnect(m_transportConnection);
    m_transport = transport;
    if (transport) {
        m_transportConnection = connect(transport, &core::Transport::positionChanged, this,
                                        [this](qint64 frame) { setPlayhead(frame); });
        setPlayhead(transport->position());
    } else {
        setPlayhead(0);
    }
}

QString TimelineController::timecode(qint64 frame) const
{
    return formatTimecode(frame, m_project.frameRate());
}

qint64 TimelineController::frameAtX(double x) const
{
    return std::max<qint64>(0, static_cast<qint64>(std::floor((x + m_scrollX) / m_pixelsPerFrame)));
}

double TimelineController::xOfFrame(qint64 frame) const
{
    return static_cast<double>(frame) * m_pixelsPerFrame - m_scrollX;
}

int TimelineController::rowAtY(double y) const
{
    if (y < 0.0)
        return -1;
    const auto row = static_cast<int>((y + m_scrollY) / trackHeight());
    return row < m_model->trackOrder().size() ? row : -1;
}

QString TimelineController::trackKind(int row) const
{
    const core::Track* track = m_project.findTrack(m_model->trackAtRow(row));
    if (!track)
        return {};
    return track->kind == core::TrackKind::Video ? u"video"_s : u"audio"_s;
}

void TimelineController::seek(qint64 frame)
{
    if (!canSeek())
        return;
    // Scrubbing sends many seeks; they are not worth a log line each.
    const QuietCommandScope quiet;
    executeReported(m_bus, kSeekCommand, QJsonObject{{u"frame"_s, std::max<qint64>(0, frame)}});
}

void TimelineController::select(const QString& clipId, bool additive)
{
    if (additive)
        m_selection.toggle(clipId);
    else
        m_selection.select(clipId);
}

void TimelineController::clearSelection()
{
    m_selection.clear();
}

void TimelineController::removeSelected()
{
    const QStringList clipIds = m_selection.clipIds();
    if (clipIds.isEmpty())
        return;
    const core::UndoGroup group(m_bus.undoStack(),
                                clipIds.size() == 1 ? u"Remove Clip"_s : u"Remove %1 Clips"_s.arg(clipIds.size()));
    for (const QString& clipId : clipIds) {
        if (!run(u"timeline.remove_clip"_s, QJsonObject{{u"clipId"_s, clipId}}))
            break;
    }
}

bool TimelineController::setTrackMuted(const QString& trackId, bool muted)
{
    return run(u"timeline.set_track_flags"_s, QJsonObject{{u"trackId"_s, trackId}, {u"muted"_s, muted}});
}

bool TimelineController::setTrackHidden(const QString& trackId, bool hidden)
{
    return run(u"timeline.set_track_flags"_s, QJsonObject{{u"trackId"_s, trackId}, {u"hidden"_s, hidden}});
}

qint64 TimelineController::snapPosition(const QString& clipId, qint64 position)
{
    position = std::max<qint64>(0, position);
    const core::Clip* clip = m_project.findClip(clipId);
    if (!m_snapping || !clip) {
        clearSnapGuide();
        return position;
    }
    qint64 startGuide = -1;
    qint64 endGuide = -1;
    const qint64 snappedStart = snapFrame(position, clipId, &startGuide);
    const qint64 snappedEnd = snapFrame(position + clip->length(), clipId, &endGuide) - clip->length();
    const qint64 startDelta = startGuide >= 0 ? std::abs(snappedStart - position) : std::numeric_limits<qint64>::max();
    const qint64 endDelta = endGuide >= 0 ? std::abs(snappedEnd - position) : std::numeric_limits<qint64>::max();
    qint64 result = position;
    qint64 guide = -1;
    if (startGuide >= 0 && startDelta <= endDelta) {
        result = snappedStart;
        guide = startGuide;
    } else if (endGuide >= 0 && snappedEnd >= 0) {
        result = snappedEnd;
        guide = endGuide;
    }
    if (guide != m_snapGuide) {
        m_snapGuide = guide;
        Q_EMIT snapGuideChanged();
    }
    return result;
}

void TimelineController::clearSnapGuide()
{
    if (m_snapGuide < 0)
        return;
    m_snapGuide = -1;
    Q_EMIT snapGuideChanged();
}

QString TimelineController::trackForMove(const QString& clipId, int row) const
{
    const core::Clip* clip = m_project.findClip(clipId);
    if (!clip)
        return {};
    const core::Track* current = m_project.findTrack(clip->trackId);
    const core::Track* target = m_project.findTrack(m_model->trackAtRow(row));
    if (!current || !target || target->kind != current->kind)
        return clip->trackId;
    return target->id;
}

bool TimelineController::moveClip(const QString& clipId, const QString& trackId, qint64 position)
{
    clearSnapGuide();
    const core::Clip* clip = m_project.findClip(clipId);
    if (!clip)
        return false;
    if (clip->trackId == trackId && clip->position == position)
        return true;
    return run(
        u"timeline.move_clip"_s,
        QJsonObject{{u"clipId"_s, clipId}, {u"trackId"_s, trackId}, {u"position"_s, std::max<qint64>(0, position)}});
}

bool TimelineController::addClip(const QString& mediaId, const QString& trackId, qint64 position)
{
    const auto result = runForResult(
        u"timeline.add_clip"_s,
        QJsonObject{{u"mediaId"_s, mediaId}, {u"trackId"_s, trackId}, {u"position"_s, std::max<qint64>(0, position)}});
    if (!result)
        return false;
    m_selection.select(result->value(u"clipId").toString());
    return true;
}

void TimelineController::zoomAt(double factor, double anchorX)
{
    if (factor <= 0.0)
        return;
    const double anchorFrame = (anchorX + m_scrollX) / m_pixelsPerFrame;
    setPixelsPerFrame(m_pixelsPerFrame * factor);
    setScrollX(anchorFrame * m_pixelsPerFrame - anchorX);
}

void TimelineController::zoomToFit()
{
    const qint64 fps = std::max<qint64>(1, std::llround(frameRate()));
    const qint64 frames = std::max<qint64>(duration(), 10 * fps);
    setPixelsPerFrame(std::max(1.0, m_viewportWidth - 40.0) / static_cast<double>(frames));
    setScrollX(0.0);
}

void TimelineController::scrollBy(double dx)
{
    setScrollX(m_scrollX + dx);
}

void TimelineController::setScrollX(double x)
{
    const double clamped = std::clamp(x, 0.0, maxScrollX());
    if (qFuzzyCompare(clamped + 1.0, m_scrollX + 1.0))
        return;
    m_scrollX = clamped;
    Q_EMIT scrollChanged();
}

void TimelineController::scrollTracksBy(double dy)
{
    setScrollY(m_scrollY + dy);
}

void TimelineController::setScrollY(double y)
{
    const double clamped = std::clamp(y, 0.0, maxScrollY());
    if (qFuzzyCompare(clamped + 1.0, m_scrollY + 1.0))
        return;
    m_scrollY = clamped;
    Q_EMIT scrollYChanged();
}

bool TimelineController::updateDrop(const QString& mediaId, double x, double y)
{
    const core::MediaItem* media = m_project.findMedia(mediaId);
    const int row = rowAtY(y);
    const core::Track* track = m_project.findTrack(m_model->trackAtRow(row));
    const qint64 length = media ? std::max<qint64>(1, media->info.durationFrames) : 0;
    qint64 frame = frameAtX(x);
    if (m_snapping && media) {
        qint64 guide = -1;
        const qint64 snapped = snapFrame(frame, {}, &guide);
        if (guide >= 0) {
            frame = snapped;
        } else {
            const qint64 snappedEnd = snapFrame(frame + length, {}, &guide) - length;
            if (guide >= 0 && snappedEnd >= 0)
                frame = snappedEnd;
        }
    }
    bool valid = media && track && trackAccepts(*track, *media);
    if (valid) {
        for (const core::Clip& clip : m_project.clips()) {
            if (clip.trackId == track->id && clip.position < frame + length && frame < clip.end()) {
                valid = false;
                break;
            }
        }
    }
    m_dropActive = media && track;
    m_dropValid = valid;
    m_dropRow = row;
    m_dropFrame = frame;
    m_dropLength = length;
    m_dropTrackId = track ? track->id : QString();
    Q_EMIT dropPreviewChanged();
    return valid;
}

void TimelineController::clearDrop()
{
    if (!m_dropActive && m_dropRow < 0)
        return;
    m_dropActive = false;
    m_dropValid = false;
    m_dropRow = -1;
    m_dropTrackId.clear();
    Q_EMIT dropPreviewChanged();
}

bool TimelineController::commitDrop(const QString& mediaId)
{
    const bool valid = m_dropValid;
    const QString trackId = m_dropTrackId;
    const qint64 frame = m_dropFrame;
    clearDrop();
    return valid && addClip(mediaId, trackId, frame);
}

void TimelineController::setPlayhead(qint64 frame)
{
    if (frame == m_playhead)
        return;
    m_playhead = frame;
    Q_EMIT playheadChanged();
    // Page along while playing so the playhead stays in view.
    if (m_transport && m_transport->state() == core::Transport::State::Playing) {
        const double x = xOfFrame(frame);
        if (x < 0.0 || x > m_viewportWidth - 24.0)
            setScrollX(static_cast<double>(frame) * m_pixelsPerFrame - m_viewportWidth * 0.1);
    }
}

void TimelineController::setPixelsPerFrame(double value)
{
    const double clamped = std::clamp(value, kMinPixelsPerFrame, kMaxPixelsPerFrame);
    if (qFuzzyCompare(clamped, m_pixelsPerFrame))
        return;
    m_pixelsPerFrame = clamped;
    Q_EMIT zoomChanged();
}

double TimelineController::maxScrollX() const
{
    const qint64 fps = std::max<qint64>(1, std::llround(frameRate()));
    const double content = static_cast<double>(std::max(duration(), m_playhead) + kTailSeconds * fps) *
                           m_pixelsPerFrame;
    return std::max(0.0, content - m_viewportWidth);
}

double TimelineController::maxScrollY() const
{
    const auto content = static_cast<double>(m_model->trackOrder().size() * trackHeight());
    return std::max(0.0, content - m_viewportHeight);
}

qint64 TimelineController::snapFrame(qint64 frame, const QString& ignoreClipId, qint64* guide) const
{
    const double threshold = kSnapPixels / m_pixelsPerFrame;
    double bestDistance = threshold + 1.0;
    qint64 best = frame;
    *guide = -1;
    const auto consider = [&](qint64 candidate) {
        const auto distance = static_cast<double>(std::abs(candidate - frame));
        if (distance <= threshold && distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
            *guide = candidate;
        }
    };
    consider(0);
    consider(m_playhead);
    for (const core::Clip& clip : m_project.clips()) {
        if (clip.id == ignoreClipId)
            continue;
        consider(clip.position);
        consider(clip.end());
    }
    return best;
}

std::optional<QJsonObject> TimelineController::runForResult(const QString& id, const QJsonObject& params)
{
    const auto run = executeInteractive(m_bus, id, params, nullptr);
    if (!run)
        return std::nullopt;
    if (!*run) {
        Q_EMIT toastRequested(run->error().message, true);
        return std::nullopt;
    }
    return **run;
}

bool TimelineController::run(const QString& id, const QJsonObject& params)
{
    return runForResult(id, params).has_value();
}

}  // namespace mikserstudio::ui
