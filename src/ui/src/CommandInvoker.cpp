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

namespace {

// True when the user chose "Continue" in a question box over `parent` (the active window when null).
bool ask(const QString& title, const QString& question, QWidget* parent)
{
    QMessageBox box(QMessageBox::Question, title, question, QMessageBox::NoButton,
                    parent ? parent : QApplication::activeWindow());
    QPushButton* proceed = box.addButton(u"Continue"_s, QMessageBox::AcceptRole);
    QPushButton* cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == proceed;
}

}  // namespace

bool confirmCommand(const core::CommandBus& bus, const QString& id, QWidget* parent)
{
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec || spec->confirmation.isEmpty())
        return true;
    return ask(spec->title, spec->confirmation, parent);
}

std::optional<std::expected<QJsonObject, core::CommandError>> executeAsking(core::CommandBus& bus, const QString& id,
                                                                            const QJsonObject& params, QWidget* parent)
{
    // Copies, because the run may unregister the command.
    const core::CommandSpec* spec = bus.spec(id);
    const QString title = spec ? spec->title : QString();
    const QString agreement = spec ? spec->confirmationParam : QString();
    QJsonObject given = params;
    if (!agreement.isEmpty())
        given.remove(agreement);  // Only the user's answer below gives it.
    auto result = bus.execute(id, given);
    if (result || result.error().code != core::CommandErrorCode::NeedsConfirmation || agreement.isEmpty())
        return result;
    if (!ask(title, result.error().message, parent))
        return std::nullopt;
    given.insert(agreement, true);
    return bus.execute(id, given);
}

std::optional<std::expected<QJsonObject, core::CommandError>> executeInteractive(core::CommandBus& bus,
                                                                                 const QString& id,
                                                                                 const QJsonObject& params,
                                                                                 QWidget* parent)
{
    if (!confirmCommand(bus, id, parent))
        return std::nullopt;
    return executeAsking(bus, id, params, parent);
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

namespace {

// True when the dialog has something to ask: a parameter besides the agreement the person gives by
// answering the command's question.
bool hasFormParams(const core::CommandSpec& spec)
{
    QJsonObject properties = spec.paramsSchema.value(u"properties").toObject();
    properties.remove(spec.confirmationParam);
    return !properties.isEmpty();
}

}  // namespace

std::optional<std::expected<QJsonObject, core::CommandError>> invokeCommand(core::CommandBus& bus, const QString& id,
                                                                            QWidget* parent)
{
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec)
        return std::nullopt;
    if (!needsParamsDialog(*spec)) {
        auto run = executeInteractive(bus, id, {}, parent);
        // A command whose params are all optional may still need one now, such as "Save project"
        // before the project has a file: it answers invalid_params, and the dialog asks for it.
        spec = bus.spec(id);
        if (!run || *run || run->error().code != core::CommandErrorCode::InvalidParams || !spec ||
            !hasFormParams(*spec))
            return run;
    } else if (!confirmCommand(bus, id, parent)) {
        return std::nullopt;
    }
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
