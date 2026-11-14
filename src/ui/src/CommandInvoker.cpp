// Interactive command runs: the confirmation box and the run on the command bus.
#include "CommandInvoker.h"

#include "ParamsDialog.h"

#include "mikserstudio/core/CommandBus.h"

#include <QApplication>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>
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

// What a run that may need the person's agreement keeps: the question box's title and the
// agreement's parameter, which only the person's answer gives.
struct Asking {
    QString title;
    QString agreement;
    QJsonObject params;

    Asking(const core::CommandBus& bus, const QString& id, QJsonObject given)
        : params(std::move(given))
    {
        // Copies, because the run may unregister the command.
        if (const core::CommandSpec* spec = bus.spec(id)) {
            title = spec->title;
            agreement = spec->confirmationParam;
        }
        if (!agreement.isEmpty())
            params.remove(agreement);
    }

    // Whether the run answered with a question to put to the person.
    [[nodiscard]] bool asks(const std::expected<QJsonObject, core::CommandError>& result) const
    {
        return !result && result.error().code == core::CommandErrorCode::NeedsConfirmation && !agreement.isEmpty();
    }

    // The params for the run after the person agreed.
    [[nodiscard]] QJsonObject agreed() const
    {
        QJsonObject again = params;
        again.insert(agreement, true);
        return again;
    }
};

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
    const Asking asking(bus, id, params);
    auto result = bus.execute(id, asking.params);
    if (!asking.asks(result))
        return result;
    if (!ask(asking.title, result.error().message, parent))
        return std::nullopt;
    return bus.execute(id, asking.agreed());
}

void executeAskingLater(core::CommandBus& bus, const QString& id, const QJsonObject& params, QWidget* parent,
                        const InteractiveReply& reply)
{
    QObject* receiver = parent ? static_cast<QObject*>(parent) : qApp;
    const auto answer = [reply](InteractiveResult result) {
        if (reply)
            reply(std::move(result));
    };
    const Asking asking(bus, id, params);
    bus.executeAsync(id, asking.params, core::CallContext::ui(), receiver,
                     [&bus, id, asking, guard = QPointer<QWidget>(parent), receiver,
                      answer](std::expected<QJsonObject, core::CommandError> result) {
                         if (!asking.asks(result)) {
                             answer(std::move(result));
                             return;
                         }
                         if (!ask(asking.title, result.error().message, guard)) {
                             answer(std::nullopt);
                             return;
                         }
                         bus.executeAsync(id, asking.agreed(), core::CallContext::ui(), receiver,
                                          [answer](std::expected<QJsonObject, core::CommandError> again) {
                                              answer(std::move(again));
                                          });
                     });
}

void executeInteractiveLater(core::CommandBus& bus, const QString& id, const QJsonObject& params, QWidget* parent,
                             const InteractiveReply& reply)
{
    if (!confirmCommand(bus, id, parent)) {
        if (reply)
            reply(std::nullopt);
        return;
    }
    executeAskingLater(bus, id, params, parent, reply);
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

// Asks for the command's params in a ParamsDialog, which runs it.
void runParamsDialog(core::CommandBus& bus, const QString& id, QWidget* parent, const InteractiveReply& reply)
{
    // A question box ran an event loop; the command may be gone.
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec) {
        reply(std::nullopt);
        return;
    }
    ParamsDialog dialog(bus, *spec, parent);
    if (dialog.exec() != QDialog::Accepted) {
        reply(std::nullopt);
        return;
    }
    reply(dialog.outcome());
}

}  // namespace

void invokeCommand(core::CommandBus& bus, const QString& id, QWidget* parent, InteractiveReply reply)
{
    const InteractiveReply answer = [reply = std::move(reply)](InteractiveResult result) {
        if (reply)
            reply(std::move(result));
    };
    const core::CommandSpec* spec = bus.spec(id);
    if (!spec) {
        answer(std::nullopt);
        return;
    }
    if (needsParamsDialog(*spec)) {
        if (!confirmCommand(bus, id, parent)) {
            answer(std::nullopt);
            return;
        }
        runParamsDialog(bus, id, parent, answer);
        return;
    }
    executeInteractiveLater(bus, id, {}, parent,
                            [&bus, id, guard = QPointer<QWidget>(parent), answer](InteractiveResult run) {
                                // A command whose params are all optional may still need one now, such as "Save
                                // project" before the project has a file: it answers invalid_params, and the dialog
                                // asks for it.
                                const core::CommandSpec* now = bus.spec(id);
                                if (!run || *run || run->error().code != core::CommandErrorCode::InvalidParams ||
                                    !now || !hasFormParams(*now)) {
                                    answer(std::move(run));
                                    return;
                                }
                                runParamsDialog(bus, id, guard, answer);
                            });
}

}  // namespace mikserstudio::ui
