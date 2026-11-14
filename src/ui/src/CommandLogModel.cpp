// The command log's rows: recording runs and failures from the command bus, folding repeats, trimming old
// rows.
#include "CommandLogModel.h"

#include "CommandText.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/ui/Theme.h"

#include <QBrush>
#include <QFont>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr qsizetype kParamsChars = 80;
constexpr qsizetype kResultChars = 120;
constexpr qsizetype kTooltipChars = 4000;

}  // namespace

CommandLogModel::CommandLogModel(core::CommandBus& bus, QObject* parent)
    : QAbstractTableModel(parent)
    , m_bus(bus)
{
    connect(&bus, &core::CommandBus::executed, this,
            [this](const QString& id, const QJsonObject& params, const QJsonObject& result) {
                if (isQuietCommand(m_bus, id))
                    return;
                append({QDateTime::currentDateTime(), id, commandTitle(m_bus, id), params, true,
                        resultSummary(result, kResultChars)});
            });
    connect(&bus, &core::CommandBus::failed, this,
            [this](const QString& id, const QJsonObject& params, const core::CommandError& error) {
                if (isQuietCommand(m_bus, id))
                    return;
                append({QDateTime::currentDateTime(), id, commandTitle(m_bus, id), params, false,
                        u"%1: %2"_s.arg(core::toString(error.code), error.message)});
            });
}

int CommandLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int CommandLogModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant CommandLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry& entry = m_entries.at(index.row());
    const ThemeTokens& theme = darkTheme();
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case TimeColumn:
            return entry.time.toString(u"HH:mm:ss"_s);
        case CommandColumn:
            return entry.repeat > 1 ? u"%1  ×%2"_s.arg(entry.id).arg(entry.repeat) : entry.id;
        case ParamsColumn:
            return compactJson(entry.params, kParamsChars);
        case ResultColumn:
            return entry.message;
        default:
            return {};
        }
    case Qt::ToolTipRole:
        switch (index.column()) {
        case CommandColumn:
            return entry.title;
        case ParamsColumn:
            return compactJson(entry.params, kTooltipChars);
        case ResultColumn:
            return entry.message;
        default:
            return entry.time.toString(Qt::ISODateWithMs);
        }
    case Qt::ForegroundRole:
        if (!entry.ok && index.column() == ResultColumn)
            return QBrush(theme.danger);
        if (index.column() == TimeColumn || index.column() == ParamsColumn)
            return QBrush(theme.textMuted);
        return {};
    case Qt::FontRole:
        if (index.column() == TimeColumn || index.column() == ParamsColumn) {
            QFont font(theme.monoFontFamily);
            font.setPixelSize(theme.fontSizeSmall);
            return font;
        }
        return {};
    default:
        return {};
    }
}

QVariant CommandLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case TimeColumn:
        return u"Time"_s;
    case CommandColumn:
        return u"Command"_s;
    case ParamsColumn:
        return u"Params"_s;
    case ResultColumn:
        return u"Result"_s;
    default:
        return {};
    }
}

void CommandLogModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

void CommandLogModel::append(Entry entry)
{
    if (!m_entries.isEmpty()) {
        Entry& last = m_entries.last();
        if (last.ok && entry.ok && last.id == entry.id && last.params == entry.params) {
            ++last.repeat;
            last.time = entry.time;
            last.message = entry.message;
            const int row = static_cast<int>(m_entries.size() - 1);
            Q_EMIT dataChanged(index(row, 0), index(row, ColumnCount - 1));
            return;
        }
    }
    if (m_entries.size() >= kMaxEntries) {
        beginRemoveRows({}, 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
    }
    const int row = static_cast<int>(m_entries.size());
    beginInsertRows({}, row, row);
    m_entries.append(std::move(entry));
    endInsertRows();
}

}  // namespace mikserstudio::ui
