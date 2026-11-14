// The media bin panel: the media items with import by button or file drop, remove, and drag or double-click onto
// the timeline.
#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QTreeView;

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class MediaBinModel;

// "Media" panel: the project's media bin. Import adds files through "project.import_media"
// (also by dropping files on the panel); rows drag onto the timeline; double-click appends the
// media to the end of its first matching track ("timeline.add_clip"); Delete removes it
// ("project.remove_media").
class MediaBinPanel final : public QWidget {
    Q_OBJECT

public:
    // Picks files to import; the default shows a QFileDialog. Replaceable for tests.
    using FilePicker = std::function<QStringList(QWidget* parent)>;

    MediaBinPanel(core::CommandBus& bus, const core::ProjectModel& project, QWidget* parent = nullptr);

    void setFilePicker(FilePicker picker) { m_pickFiles = std::move(picker); }
    void importFiles(const QStringList& paths);
    void appendToTimeline(const QString& mediaId);
    void removeSelected();
    [[nodiscard]] QTreeView* view() const { return m_view; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateEmptyState();
    // The name column gets the room first: the kind column, which the swatch beside each name also
    // shows, hides while it would leave too little room for a typical clip name.
    void updateColumns();

    core::CommandBus& m_bus;
    const core::ProjectModel& m_project;
    MediaBinModel* m_model;
    QTreeView* m_view;
    QLabel* m_empty;
    FilePicker m_pickFiles;
};

}  // namespace mikserstudio::ui
