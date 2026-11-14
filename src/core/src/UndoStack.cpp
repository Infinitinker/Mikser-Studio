// The undo history and the compound edit that groups use.
#include "mikserstudio/core/UndoStack.h"

#include "Logging.h"

#include <ranges>
#include <utility>

namespace mikserstudio::core {

namespace {

// The edits of one undo group, reverted in reverse order.
class CompoundEdit final : public UndoableEdit {
public:
    CompoundEdit(QString text, std::vector<std::unique_ptr<UndoableEdit>> edits)
        : m_text(std::move(text))
        , m_edits(std::move(edits))
    {
    }

    void undo() override
    {
        for (const auto& edit : std::views::reverse(m_edits))
            edit->undo();
    }
    void redo() override
    {
        for (const auto& edit : m_edits)
            edit->redo();
    }
    [[nodiscard]] QString text() const override { return m_text; }

private:
    QString m_text;
    std::vector<std::unique_ptr<UndoableEdit>> m_edits;
};

}  // namespace

UndoStack::UndoStack(QObject* parent)
    : QObject(parent)
{
}

UndoStack::~UndoStack() = default;

void UndoStack::push(std::unique_ptr<UndoableEdit> edit, QStringList origins)
{
    if (!edit) {
        qCWarning(lcUndo) << "Ignoring an empty undo edit";
        return;
    }
    if (isGrouping()) {
        m_groupEdits.push_back({std::move(edit), std::move(origins)});
        updateClean();
        return;
    }
    if (m_cleanIndex && *m_cleanIndex > m_index)
        m_cleanIndex.reset();  // The clean point was in the redo branch this push discards.
    m_edits.erase(m_edits.begin() + static_cast<std::ptrdiff_t>(m_index), m_edits.end());
    m_edits.push_back({std::move(edit), std::move(origins)});
    m_index = m_edits.size();
    Q_EMIT changed();
    updateClean();
}

void UndoStack::beginGroup(const QString& text)
{
    if (m_groupDepth++ == 0)
        m_groupText = text;
}

void UndoStack::endGroup()
{
    if (!isGrouping()) {
        qCWarning(lcUndo) << "Ignoring endGroup() without a matching beginGroup()";
        return;
    }
    if (--m_groupDepth > 0)
        return;
    auto steps = std::exchange(m_groupEdits, {});
    QString text = std::exchange(m_groupText, {});
    if (steps.empty())
        return;
    QStringList origins = steps.front().origins;
    std::vector<std::unique_ptr<UndoableEdit>> edits;
    edits.reserve(steps.size());
    for (Step& step : steps)
        edits.push_back(std::move(step.edit));
    push(std::make_unique<CompoundEdit>(std::move(text), std::move(edits)), std::move(origins));
}

bool UndoStack::rollBackGroup()
{
    if (m_groupDepth != 1) {
        qCWarning(lcUndo) << "Ignoring rollBackGroup() with" << m_groupDepth << "groups open; it needs exactly one";
        return false;
    }
    m_groupDepth = 0;
    m_groupText.clear();
    const auto steps = std::exchange(m_groupEdits, {});
    for (const Step& step : std::views::reverse(steps))
        step.edit->undo();
    updateClean();
    return true;
}

bool UndoStack::canUndo() const
{
    return !isGrouping() && m_index > 0;
}

bool UndoStack::canRedo() const
{
    return !isGrouping() && m_index < m_edits.size();
}

QString UndoStack::undoText() const
{
    return canUndo() ? m_edits[m_index - 1].edit->text() : QString();
}

QString UndoStack::redoText() const
{
    return canRedo() ? m_edits[m_index].edit->text() : QString();
}

QStringList UndoStack::undoOrigins() const
{
    return canUndo() ? m_edits[m_index - 1].origins : QStringList();
}

QStringList UndoStack::redoOrigins() const
{
    return canRedo() ? m_edits[m_index].origins : QStringList();
}

bool UndoStack::undo()
{
    if (!canUndo())
        return false;
    --m_index;
    m_edits[m_index].edit->undo();
    Q_EMIT changed();
    updateClean();
    return true;
}

bool UndoStack::redo()
{
    if (!canRedo())
        return false;
    m_edits[m_index].edit->redo();
    ++m_index;
    Q_EMIT changed();
    updateClean();
    return true;
}

void UndoStack::clear()
{
    m_groupEdits.clear();
    m_cleanIndex.reset();
    if (!m_edits.empty()) {
        m_edits.clear();
        m_index = 0;
        Q_EMIT changed();
    }
    updateClean();
}

void UndoStack::setClean()
{
    m_cleanIndex = m_index;
    updateClean();
}

bool UndoStack::isClean() const
{
    return m_cleanIndex == m_index && m_groupEdits.empty();
}

void UndoStack::updateClean()
{
    const bool clean = isClean();
    if (clean == m_clean)
        return;
    m_clean = clean;
    Q_EMIT cleanChanged(clean);
}

}  // namespace mikserstudio::core
