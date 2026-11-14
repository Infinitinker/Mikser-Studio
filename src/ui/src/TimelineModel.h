// The timeline's clips as a list model for QML, kept in sync with the project.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

namespace mikserstudio::ui {

class ClipSelection;

// The timeline clips for QML, one row per clip, kept in sync with the ProjectModel signals
// (clips are inserted, removed and changed in place; a project reset resets the model).
//
// Tracks are ProjectModel::tracks(), shown top to bottom as video tracks from the highest down
// (V3 above V2 above V1), then audio tracks in order (A1, A2, A3); "trackRow" is the clip's track
// in that order and follows tracks being added or removed. "trackOff" is true while the clip's
// track is hidden (video) or muted (audio).
class TimelineModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        ClipIdRole = Qt::UserRole + 1,
        MediaIdRole,
        NameRole,
        TrackIdRole,
        TrackRowRole,
        PositionRole,
        LengthRole,
        InRole,
        OutRole,
        KindRole,  // "video" | "audio" | "image"
        ColorRole,
        SelectedRole,
        GeneratedRole,  // The media came from an AI job.
        TrackOffRole,
    };

    TimelineModel(const core::ProjectModel& project, const ClipSelection& selection, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Track ids top to bottom.
    [[nodiscard]] QStringList trackOrder() const { return m_trackOrder; }
    [[nodiscard]] int rowOfTrack(const QString& trackId) const;
    [[nodiscard]] QString trackAtRow(int row) const;
    [[nodiscard]] int rowOfClip(const QString& clipId) const;

Q_SIGNALS:
    void tracksChanged();

private:
    void onClipAdded(const QString& id);
    void onClipRemoved(const QString& id);
    void onClipChanged(const QString& id);
    void onTrackChanged(const QString& id);
    void reset();
    void updateTrackOrder();

    const core::ProjectModel& m_project;
    const ClipSelection& m_selection;
    QStringList m_clipIds;  // Row order.
    QStringList m_trackOrder;  // Top to bottom.
};

}  // namespace mikserstudio::ui
