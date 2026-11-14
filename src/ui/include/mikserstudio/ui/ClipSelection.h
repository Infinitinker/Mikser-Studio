// The selection of timeline clips, shared by the timeline, properties and AI panels.
#pragma once

#include <QObject>
#include <QStringList>

namespace mikserstudio::core {
class ProjectModel;
}

namespace mikserstudio::ui {

// The selected timeline clips, shared by the timeline, properties and AI panels. Clips removed
// from the project leave the selection automatically. The first selected clip is "current".
class ClipSelection final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList clipIds READ clipIds NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)

public:
    explicit ClipSelection(const core::ProjectModel& project, QObject* parent = nullptr);

    [[nodiscard]] QStringList clipIds() const { return m_clipIds; }
    [[nodiscard]] QString current() const { return m_clipIds.value(0); }
    [[nodiscard]] Q_INVOKABLE bool contains(const QString& clipId) const { return m_clipIds.contains(clipId); }
    [[nodiscard]] bool isEmpty() const { return m_clipIds.isEmpty(); }

    // Ids that are not clips of the project are ignored.
    Q_INVOKABLE void select(const QString& clipId);  // Replaces the selection.
    Q_INVOKABLE void toggle(const QString& clipId);  // Adds or removes one clip.
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void changed();

private:
    void setClipIds(QStringList ids);
    void dropMissing();

    const core::ProjectModel& m_project;
    QStringList m_clipIds;
};

}  // namespace mikserstudio::ui
