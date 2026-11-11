#include "CommandPaletteModel.h"

#include "FuzzyMatch.h"

#include <QKeySequence>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr int kTitleBonus = 12;

std::optional<int> score(const QString& query, const core::CommandSpec& spec)
{
    std::optional<int> best;
    const auto consider = [&best](std::optional<int> candidate, int bonus) {
        if (candidate && (!best || *candidate + bonus > *best))
            best = *candidate + bonus;
    };
    consider(fuzzyScore(query, spec.title), kTitleBonus);
    consider(fuzzyScore(query, spec.id), 0);
    consider(fuzzyScore(query, spec.category + u' ' + spec.title), 0);
    return best;
}

}  // namespace

void CommandPaletteModel::setSpecs(QList<core::CommandSpec> specs)
{
    m_specs = std::move(specs);
    refilter();
}

void CommandPaletteModel::setQuery(const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed == m_query)
        return;
    m_query = trimmed;
    refilter();
}

int CommandPaletteModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant CommandPaletteModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const core::CommandSpec& spec = m_specs.at(m_rows.at(index.row()));
    switch (role) {
    case Qt::DisplayRole:
        return spec.title;
    case Qt::ToolTipRole:
    case DescriptionRole:
        return spec.description;
    case IdRole:
        return spec.id;
    case CategoryRole:
        return spec.category;
    case ShortcutRole:
        return QKeySequence(spec.shortcut, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    default:
        return {};
    }
}

QString CommandPaletteModel::idAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_specs.at(m_rows.at(row)).id : QString();
}

QStringList CommandPaletteModel::ids() const
{
    QStringList list;
    for (const qsizetype row : m_rows)
        list.append(m_specs.at(row).id);
    return list;
}

void CommandPaletteModel::refilter()
{
    beginResetModel();
    m_rows.clear();
    QList<std::pair<int, qsizetype>> ranked;
    for (qsizetype i = 0; i < m_specs.size(); ++i) {
        if (const auto value = score(m_query, m_specs.at(i)))
            ranked.append({*value, i});
    }
    std::ranges::stable_sort(ranked, [this](const auto& a, const auto& b) {
        if (!m_query.isEmpty() && a.first != b.first)
            return a.first > b.first;
        const core::CommandSpec& left = m_specs.at(a.second);
        const core::CommandSpec& right = m_specs.at(b.second);
        if (m_query.isEmpty() && left.category != right.category)
            return left.category.localeAwareCompare(right.category) < 0;
        return left.title.localeAwareCompare(right.title) < 0;
    });
    for (const auto& [value, row] : ranked)
        m_rows.append(row);
    endResetModel();
}

}  // namespace mikserstudio::ui
