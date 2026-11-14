// An immutable copy of the project at one revision, for readers on any thread.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace mikserstudio::core {

// The project as it was after one change. ProjectModel publishes a new one after every change;
// a snapshot itself never changes, so any thread may read it. Its lists share their data with the
// model's until the model changes them (Qt's implicit sharing), so taking one is cheap.
struct ProjectSnapshot {
    // Grows with every change; a new or opened project goes on counting.
    quint64 revision = 0;
    // Changes when a new project is started or one is opened, so a reader knows that ids it kept
    // belong to another project.
    QString session;
    Rational frameRate{30, 1};
    int width = 1920;
    int height = 1080;
    QString filePath;
    QList<MediaItem> media;
    QList<Track> tracks;
    QList<Clip> clips;  // Sorted by track, position.

    [[nodiscard]] const MediaItem* findMedia(const QString& id) const;
    [[nodiscard]] const Track* findTrack(const QString& id) const;
    [[nodiscard]] const Clip* findClip(const QString& id) const;
    // The clips that overlap the frames [from, to), on `trackId` or, when it is empty, on every
    // track; in the order of `clips`.
    [[nodiscard]] QList<Clip> clipsInRange(FramePos from, FramePos to, const QString& trackId = {}) const;
    // The clips that use the media item.
    [[nodiscard]] QList<Clip> clipsOfMedia(const QString& mediaId) const;

    // The project JSON (as ProjectModel::toJson()) with "session" and "revision".
    [[nodiscard]] QJsonObject toJson() const;
};

}  // namespace mikserstudio::core
