// The media bin as a table model (name, duration, kind) over the project's media items; rows can be dragged
// to the timeline.
#pragma once

#include <QAbstractTableModel>

namespace mikserstudio::core {
class ProjectModel;
}

namespace mikserstudio::ui {

// The project's media bin as a table (name, duration, kind), in bin order. Rows drag as
// kMediaIdMimeType carrying the media id, which the timeline accepts.
class MediaBinModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { NameColumn, DurationColumn, KindColumn, ColumnCount };
    enum Role { MediaIdRole = Qt::UserRole + 1 };
    static constexpr auto kMediaIdMimeType = "application/x-mikserstudio-media-id";

    explicit MediaBinModel(const core::ProjectModel& project, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

    [[nodiscard]] QString mediaIdAt(int row) const;

private:
    void reload();

    const core::ProjectModel& m_project;
};

}  // namespace mikserstudio::ui
