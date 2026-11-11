#pragma once

#include "mikserstudio/core/Command.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QJsonObject>
#include <QList>

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Chronological list of the commands executed or failed on the bus (except quiet ones: read-only
// commands and UI bookkeeping, see QuietCommands.h). Identical consecutive successful runs are
// folded into one row with a repeat count. Keeps the newest kMaxEntries rows.
class CommandLogModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { TimeColumn, CommandColumn, ParamsColumn, ResultColumn, ColumnCount };
    static constexpr qsizetype kMaxEntries = 2000;

    explicit CommandLogModel(core::CommandBus& bus, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void clear();

private:
    struct Entry {
        QDateTime time;
        QString id;
        QString title;
        QJsonObject params;
        bool ok = true;
        QString message;  // Result summary or error message.
        int repeat = 1;
    };

    void append(Entry entry);

    const core::CommandBus& m_bus;
    QList<Entry> m_entries;
};

}  // namespace mikserstudio::ui
