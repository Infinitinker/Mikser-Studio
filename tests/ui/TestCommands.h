#pragma once

#include "mikserstudio/core/CommandBus.h"

#include <QJsonObject>
#include <QString>

#include <memory>
#include <utility>

namespace mikserstudio::ui::test {

// An edit that only counts how often it was undone and redone.
class CountingEdit final : public core::UndoableEdit {
public:
    explicit CountingEdit(QString text)
        : m_text(std::move(text))
    {
    }
    void undo() override {}
    void redo() override {}
    [[nodiscard]] QString text() const override { return m_text; }

private:
    QString m_text;
};

struct TestCommand {
    QString id;
    QString title;
    QString category;
    QJsonObject paramsSchema = {};
    QString shortcut = {};
    QString editText = {};  // Non-empty: the command is undoable and pushes an edit with this text.
    bool readOnly = false;
    QString confirmation = {};
};

// Registers a command that returns {"ran": <id>} and increments `*runs` (when given).
inline bool registerTestCommand(core::CommandBus& bus, const TestCommand& command, int* runs = nullptr)
{
    const bool undoable = !command.editText.isEmpty();
    return bus.registerCommand(
        core::CommandSpec{
            .id = command.id,
            .title = command.title,
            .description = command.title + QStringLiteral(" (test)."),
            .category = command.category,
            .paramsSchema = command.paramsSchema,
            .shortcut = command.shortcut,
            .undoable = undoable,
            .readOnly = command.readOnly,
            .confirmation = command.confirmation,
        },
        [id = command.id, editText = command.editText, runs](const QJsonObject&) -> core::CommandResult {
            if (runs)
                ++*runs;
            core::CommandOutcome outcome;
            outcome.result = QJsonObject{{QStringLiteral("ran"), id}};
            if (!editText.isEmpty())
                outcome.edit = std::make_unique<CountingEdit>(editText);
            return outcome;
        });
}

}  // namespace mikserstudio::ui::test
