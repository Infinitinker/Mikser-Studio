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

void UndoStack::push(std::unique_ptr<UndoableEdit> edit)
{
    if (!edit) {
        qCWarning(lcUndo) << "Ignoring an empty undo edit";
        return;
    }
    if (isGrouping()) {
        m_groupEdits.push_back(std::move(edit));
        updateClean();
        return;
    }
    if (m_cleanIndex && *m_cleanIndex > m_index)
        m_cleanIndex.reset();  // The clean point was in the redo branch this push discards.
    m_edits.erase(m_edits.begin() + static_cast<std::ptrdiff_t>(m_index), m_edits.end());
    m_edits.push_back(std::move(edit));
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
    auto edits = std::exchange(m_groupEdits, {});
    QString text = std::exchange(m_groupText, {});
    if (!edits.empty())
        push(std::make_unique<CompoundEdit>(std::move(text), std::move(edits)));
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
    return canUndo() ? m_edits[m_index - 1]->text() : QString();
}

QString UndoStack::redoText() const
{
    return canRedo() ? m_edits[m_index]->text() : QString();
}

bool UndoStack::undo()
{
    if (!canUndo())
        return false;
    --m_index;
    m_edits[m_index]->undo();
    Q_EMIT changed();
    updateClean();
    return true;
}

bool UndoStack::redo()
{
    if (!canRedo())
        return false;
    m_edits[m_index]->redo();
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
