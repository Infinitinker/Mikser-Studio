// Interactive command runs: the confirmation box and the run on the command bus.
#include "CommandInvoker.h"

#include "ParamsDialog.h"

#include "mikserstudio/core/CommandBus.h"

#include <QApplication>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>

#include <tuple>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

bool confirmCommand(const core::CommandBus& bus, const QString& id, QWidget* parent)
{
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec || spec->confirmation.isEmpty())
        return true;
    QMessageBox box(QMessageBox::Question, spec->title, spec->confirmation, QMessageBox::NoButton,
                    parent ? parent : QApplication::activeWindow());
    QPushButton* proceed = box.addButton(u"Continue"_s, QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == proceed;
}

std::optional<std::expected<QJsonObject, core::CommandError>> executeInteractive(core::CommandBus& bus,
                                                                                 const QString& id,
                                                                                 const QJsonObject& params,
                                                                                 QWidget* parent)
{
    if (!confirmCommand(bus, id, parent))
        return std::nullopt;
    return bus.execute(id, params);
}

void executeReported(core::CommandBus& bus, const QString& id, const QJsonObject& params, QWidget* parent)
{
    // Failures reach the user through CommandBus::failed (status bar and log).
    std::ignore = executeInteractive(bus, id, params, parent);
}

bool needsParamsDialog(const core::CommandSpec& spec)
{
    return !spec.paramsSchema.value(u"required").toArray().isEmpty();
}

std::optional<std::expected<QJsonObject, core::CommandError>> invokeCommand(core::CommandBus& bus, const QString& id,
                                                                            QWidget* parent)
{
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec)
        return std::nullopt;
    if (!needsParamsDialog(*spec))
        return executeInteractive(bus, id, {}, parent);
    if (!confirmCommand(bus, id, parent))
        return std::nullopt;
    // The question box ran an event loop; the command may be gone.
    spec = bus.spec(id);
    if (!spec)
        return std::nullopt;
    ParamsDialog dialog(bus, *spec, parent);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return dialog.outcome();
}

}  // namespace mikserstudio::ui
