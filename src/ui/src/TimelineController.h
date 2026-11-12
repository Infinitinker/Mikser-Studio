// The timeline's controller for QML: tracks, zoom, scroll, playhead, snapping and the actions, all run as
// commands.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include <optional>

namespace mikserstudio::core {
class CommandBus;
class Transport;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class ClipSelection;
class TimelineModel;

// Everything the timeline QML needs (context property "timeline"): the clip model, the tracks of
// the project (one row each, in TimelineModel order), zoom and scroll, the playhead, snapping and
// the actions, which all go through commands ("timeline.move_clip", "timeline.add_clip",
// "timeline.remove_clip", "timeline.set_track_flags", "program.seek"), asking first when a
// command has a confirmation.
//
// Geometry: the track area starts at headerWidth on the left and rulerHeight at the top; x and y
// values passed to frameAtX()/xOfFrame()/rowAtY() are relative to the visible track area. The
// rows scroll vertically by scrollY when they do not fit into viewportHeight.
//
// Feedback: a failed action and a snapping change by key emit toastRequested(), which the QML
// shows briefly (every time, also when the same error happens twice in a row).
class TimelineController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* model READ modelObject CONSTANT)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY frameRateChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(double pixelsPerFrame READ pixelsPerFrame NOTIFY zoomChanged)
    Q_PROPERTY(qint64 rulerStep READ rulerStep NOTIFY zoomChanged)
    Q_PROPERTY(double scrollX READ scrollX NOTIFY scrollChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY viewportWidthChanged)
    Q_PROPERTY(double scrollY READ scrollY NOTIFY scrollYChanged)
    Q_PROPERTY(double viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewportHeightChanged)
    Q_PROPERTY(int headerWidth READ headerWidth CONSTANT)
    Q_PROPERTY(int rulerHeight READ rulerHeight CONSTANT)
    Q_PROPERTY(int trackHeight READ trackHeight CONSTANT)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY canSeekChanged)
    Q_PROPERTY(bool snapping READ snapping WRITE setSnapping NOTIFY snappingChanged)
    Q_PROPERTY(qint64 snapGuide READ snapGuide NOTIFY snapGuideChanged)
    Q_PROPERTY(bool dropActive READ dropActive NOTIFY dropPreviewChanged)
    Q_PROPERTY(bool dropValid READ dropValid NOTIFY dropPreviewChanged)
    Q_PROPERTY(int dropRow READ dropRow NOTIFY dropPreviewChanged)
    Q_PROPERTY(qint64 dropFrame READ dropFrame NOTIFY dropPreviewChanged)
    Q_PROPERTY(qint64 dropLength READ dropLength NOTIFY dropPreviewChanged)

public:
    static constexpr double kMinPixelsPerFrame = 0.02;
    static constexpr double kMaxPixelsPerFrame = 40.0;
    static constexpr double kSnapPixels = 8.0;

    TimelineController(core::CommandBus& bus, const core::ProjectModel& project, ClipSelection& selection,
                       QObject* parent = nullptr);

    [[nodiscard]] TimelineModel* model() const { return m_model; }
    [[nodiscard]] QObject* modelObject() const;
    [[nodiscard]] QVariantList tracks() const;
    [[nodiscard]] double frameRate() const { return m_project.frameRate().toDouble(); }
    [[nodiscard]] qint64 duration() const;
    [[nodiscard]] qint64 playhead() const { return m_playhead; }
    [[nodiscard]] double pixelsPerFrame() const { return m_pixelsPerFrame; }
    [[nodiscard]] qint64 rulerStep() const;
    [[nodiscard]] double scrollX() const { return m_scrollX; }
    [[nodiscard]] double viewportWidth() const { return m_viewportWidth; }
    void setViewportWidth(double width);
    [[nodiscard]] double scrollY() const { return m_scrollY; }
    [[nodiscard]] double viewportHeight() const { return m_viewportHeight; }
    void setViewportHeight(double height);
    [[nodiscard]] int headerWidth() const { return 104; }
    [[nodiscard]] int rulerHeight() const { return 30; }
    [[nodiscard]] int trackHeight() const { return 54; }
    [[nodiscard]] bool canSeek() const;
    [[nodiscard]] bool snapping() const { return m_snapping; }
    void setSnapping(bool on);
    // Flips snapping and says so in a toast (the S key).
    void toggleSnapping();
    [[nodiscard]] qint64 snapGuide() const { return m_snapGuide; }
    [[nodiscard]] bool dropActive() const { return m_dropActive; }
    [[nodiscard]] bool dropValid() const { return m_dropValid; }
    [[nodiscard]] int dropRow() const { return m_dropRow; }
    [[nodiscard]] qint64 dropFrame() const { return m_dropFrame; }
    [[nodiscard]] qint64 dropLength() const { return m_dropLength; }

    // The playhead follows this transport's position; null keeps it at 0.
    void setTransport(core::Transport* transport);

    Q_INVOKABLE [[nodiscard]] QString timecode(qint64 frame) const;
    Q_INVOKABLE [[nodiscard]] qint64 frameAtX(double x) const;
    Q_INVOKABLE [[nodiscard]] double xOfFrame(qint64 frame) const;
    Q_INVOKABLE [[nodiscard]] int rowAtY(double y) const;  // y relative to the track area; -1 outside.
    Q_INVOKABLE [[nodiscard]] QString trackKind(int row) const;  // "video" | "audio" | ""

    Q_INVOKABLE void seek(qint64 frame);  // Runs "program.seek" when it exists.
    Q_INVOKABLE void select(const QString& clipId, bool additive);
    Q_INVOKABLE void clearSelection();
    // Removes every selected clip as one undo step.
    Q_INVOKABLE void removeSelected();
    // Run "timeline.set_track_flags" for one flag, keeping the other.
    Q_INVOKABLE bool setTrackMuted(const QString& trackId, bool muted);
    Q_INVOKABLE bool setTrackHidden(const QString& trackId, bool hidden);

    // Where a clip being dragged to `position` lands with snapping: its start or end moves onto
    // the nearest clip edge, the playhead or 0 within kSnapPixels. Sets snapGuide.
    Q_INVOKABLE [[nodiscard]] qint64 snapPosition(const QString& clipId, qint64 position);
    Q_INVOKABLE void clearSnapGuide();
    // The track a clip dragged to `row` goes to: that row's track when it has the clip's kind,
    // otherwise the clip's own track.
    Q_INVOKABLE [[nodiscard]] QString trackForMove(const QString& clipId, int row) const;
    Q_INVOKABLE bool moveClip(const QString& clipId, const QString& trackId, qint64 position);
    Q_INVOKABLE bool addClip(const QString& mediaId, const QString& trackId, qint64 position);

    Q_INVOKABLE void zoomAt(double factor, double anchorX);
    Q_INVOKABLE void zoomToFit();
    Q_INVOKABLE void scrollBy(double dx);
    void setScrollX(double x);
    Q_INVOKABLE void scrollTracksBy(double dy);
    void setScrollY(double y);

    // Drop preview while media from the bin is dragged over the timeline. x/y relative to the
    // track area. Returns whether the media can be dropped there.
    bool updateDrop(const QString& mediaId, double x, double y);
    void clearDrop();
    // Adds the media at the previewed place; false when the drop is not valid.
    bool commitDrop(const QString& mediaId);

Q_SIGNALS:
    void tracksChanged();
    void frameRateChanged();
    void durationChanged();
    void playheadChanged();
    void zoomChanged();
    void scrollChanged();
    void viewportWidthChanged();
    void scrollYChanged();
    void viewportHeightChanged();
    void canSeekChanged();
    void snappingChanged();
    void snapGuideChanged();
    void dropPreviewChanged();
    void toastRequested(const QString& text, bool error);

private:
    void setPlayhead(qint64 frame);
    void setPixelsPerFrame(double value);
    [[nodiscard]] double maxScrollX() const;
    [[nodiscard]] double maxScrollY() const;
    [[nodiscard]] qint64 snapFrame(qint64 frame, const QString& ignoreClipId, qint64* guide) const;
    // Runs an interactive command; a failure is shown in a toast. std::nullopt when it failed or
    // the user canceled.
    std::optional<QJsonObject> runForResult(const QString& id, const QJsonObject& params);
    bool run(const QString& id, const QJsonObject& params);

    core::CommandBus& m_bus;
    const core::ProjectModel& m_project;
    ClipSelection& m_selection;
    TimelineModel* m_model;
    QPointer<core::Transport> m_transport;
    QMetaObject::Connection m_transportConnection;

    qint64 m_playhead = 0;
    double m_pixelsPerFrame = 2.0;
    double m_scrollX = 0.0;
    double m_viewportWidth = 800.0;
    double m_scrollY = 0.0;
    double m_viewportHeight = 400.0;
    bool m_snapping = true;
    qint64 m_snapGuide = -1;
    bool m_dropActive = false;
    bool m_dropValid = false;
    int m_dropRow = -1;
    qint64 m_dropFrame = 0;
    qint64 m_dropLength = 0;
    QString m_dropTrackId;
};

}  // namespace mikserstudio::ui
