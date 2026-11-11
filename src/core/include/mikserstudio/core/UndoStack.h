#pragma once

#include "mikserstudio/core/Command.h"

#include <QObject>

#include <memory>
#include <vector>

namespace mikserstudio::core {

// Linear undo history of applied edits. Pushing after an undo discards the redo branch.
//
// Groups: between beginGroup() and the matching endGroup(), pushed edits are collected instead of
// recorded; the outermost endGroup() pushes them as one compound edit named by the outermost
// group's text (undo reverts them in reverse order). Groups nest; an empty group pushes nothing.
// While a group is open, canUndo()/canRedo() are false and undo()/redo() return false.
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

Q_SIGNALS:
    void changed();

private:
    std::vector<std::unique_ptr<UndoableEdit>> m_edits;
    std::size_t m_index = 0;  // Number of applied edits; m_edits[m_index..] are redoable.
    int m_groupDepth = 0;
    QString m_groupText;
    std::vector<std::unique_ptr<UndoableEdit>> m_groupEdits;
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
