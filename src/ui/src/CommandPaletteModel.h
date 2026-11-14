// The command palette's rows: commands filtered and ranked by a fuzzy query over title, id and category.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QAbstractListModel>
#include <QList>

namespace mikserstudio::ui {

// The command palette's rows: command specs filtered and ranked by a fuzzy query over title,
// id and category. An empty query lists every command by category, then title.
class CommandPaletteModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        CategoryRole,
        ShortcutRole,
        DescriptionRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setSpecs(QList<core::CommandSpec> specs);
    void setQuery(const QString& query);
    [[nodiscard]] QString query() const { return m_query; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;

    [[nodiscard]] QString idAt(int row) const;
    [[nodiscard]] QStringList ids() const;  // In display order.

private:
    void refilter();

    QList<core::CommandSpec> m_specs;
    QList<qsizetype> m_rows;  // Indexes into m_specs.
    QString m_query;
};

}  // namespace mikserstudio::ui
