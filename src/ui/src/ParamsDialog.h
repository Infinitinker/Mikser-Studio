#pragma once

#include "mikserstudio/core/Command.h"

#include <QDialog>
#include <QJsonObject>

#include <expected>
#include <optional>

class QLabel;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

class SchemaForm;

// Asks for a command's params with a SchemaForm and runs the command. The dialog stays open
// (showing the error) when validation or the command fails, and closes when it succeeds.
class ParamsDialog final : public QDialog {
    Q_OBJECT

public:
    ParamsDialog(core::CommandBus& bus, const core::CommandSpec& spec, QWidget* parent = nullptr);

    [[nodiscard]] SchemaForm* form() const { return m_form; }
    // The successful result, set when the dialog was accepted.
    [[nodiscard]] std::expected<QJsonObject, core::CommandError> result() const { return m_result; }

    void run();  // What the Run button does.

private:
    core::CommandBus& m_bus;
    QString m_commandId;
    SchemaForm* m_form = nullptr;
    QLabel* m_error = nullptr;
    std::expected<QJsonObject, core::CommandError> m_result;
};

}  // namespace mikserstudio::ui
