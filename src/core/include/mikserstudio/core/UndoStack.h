// The undo history: applied edits, undo and redo, and groups that combine several edits into one step.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QObject>

#include <memory>
#include <optional>
#include <vector>

namespace mikserstudio::core {

// Linear undo history of applied edits. Pushing after an undo discards the redo branch.
//
// Groups: between beginGroup() and the matching endGroup(), pushed edits are collected instead of
// recorded; the outermost endGroup() pushes them as one compound edit named by the outermost
// group's text (undo reverts them in reverse order). Groups nest; an empty group pushes nothing.
// While a group is open, canUndo()/canRedo() are false and undo()/redo() return false.
//
// The clean state: setClean() marks the current point of the history as the one saved to the
// project's file. isClean() is true while the history is back at that point and no group holds
// edits; undo and redo can leave and return to it. Pushing an edit after undoing past the clean
// point makes it unreachable, and clear() forgets it, until setClean() marks a new one.
class UndoStack final : public QObject {
    Q_OBJECT

public:
    explicit UndoStack(QObject* parent = nullptr);
    ~UndoStack() override;

    // Takes an edit that has already been applied.
    void push(std::unique_ptr<UndoableEdit> edit);

    void beginGroup(const QString& text);
    void endGroup();
    [[nodiscard]] bool isGrouping() const { return m_groupDepth > 0; }

    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;

    bool undo();  // Returns false when there is nothing to undo.
    bool redo();  // Returns false when there is nothing to redo.
    void clear();  // Also drops the edits collected by an open group; the group stays open.

    void setClean();
    [[nodiscard]] bool isClean() const;

Q_SIGNALS:
    void changed();
    void cleanChanged(bool clean);

private:
    void updateClean();

    std::vector<std::unique_ptr<UndoableEdit>> m_edits;
    std::size_t m_index = 0;  // Number of applied edits; m_edits[m_index..] are redoable.
    int m_groupDepth = 0;
    QString m_groupText;
    std::vector<std::unique_ptr<UndoableEdit>> m_groupEdits;
    std::optional<std::size_t> m_cleanIndex = 0;  // m_index at the clean point; empty when unreachable.
    bool m_clean = true;  // isClean() when cleanChanged() was last emitted.
};

// Opens an undo group for its lifetime, also when the scope is left by an exception.
class UndoGroup final {
public:
    UndoGroup(UndoStack& stack, const QString& text)
        : m_stack(stack)
    {
        m_stack.beginGroup(text);
    }
    ~UndoGroup() { m_stack.endGroup(); }
    UndoGroup(const UndoGroup&) = delete;
    UndoGroup& operator=(const UndoGroup&) = delete;

private:
    UndoStack& m_stack;
};

}  // namespace mikserstudio::core
