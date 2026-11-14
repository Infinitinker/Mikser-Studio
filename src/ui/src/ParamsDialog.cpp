// The params dialog: validates the form, runs the command and shows its error while staying open.
#include "ParamsDialog.h"

#include "CommandInvoker.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/ui/SchemaForm.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

ParamsDialog::ParamsDialog(core::CommandBus& bus, const core::CommandSpec& spec, QWidget* parent)
    : QDialog(parent)
    , m_bus(bus)
    , m_commandId(spec.id)
    , m_result(std::unexpected(core::CommandError{core::CommandErrorCode::Failed, u"The command did not run."_s}))
{
    setWindowTitle(spec.title);
    setMinimumWidth(420);

    auto* heading = new QLabel(spec.title, this);
    heading->setProperty("role", u"heading"_s);
    auto* description = new QLabel(spec.description, this);
    description->setProperty("role", u"muted"_s);
    description->setWordWrap(true);
    description->setVisible(!spec.description.isEmpty());

    // The user gives the confirmation parameter by answering the question, not in the form.
    QJsonObject schema = spec.paramsSchema;
    if (!spec.confirmationParam.isEmpty()) {
        QJsonObject properties = schema.value(u"properties"_s).toObject();
        properties.remove(spec.confirmationParam);
        schema.insert(u"properties"_s, properties);
    }
    m_form = new SchemaForm(schema, {}, this);
    m_error = new QLabel(this);
    m_error->setProperty("role", u"error"_s);
    m_error->setWordWrap(true);
    m_error->hide();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_runButton = buttons->addButton(u"Run"_s, QDialogButtonBox::AcceptRole);
    m_runButton->setProperty("role", u"primary"_s);
    m_runButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &ParamsDialog::run);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addSpacing(4);
    layout->addWidget(m_form);
    layout->addWidget(m_error);
    layout->addStretch();
    layout->addWidget(buttons);
}

void ParamsDialog::run()
{
    m_error->hide();
    if (!m_form->validate().isEmpty())
        return;
    m_runButton->setEnabled(false);  // Until the run, which may answer later, has finished.
    executeAskingLater(m_bus, m_commandId, m_form->value(), this, [this](InteractiveResult run) {
        m_runButton->setEnabled(true);
        if (!run)
            return;  // The user did not agree; they may change the params.
        m_result = *run;
        if (m_result) {
            accept();
            return;
        }
        m_error->setText(m_result.error().message);
        m_error->show();
    });
}

}  // namespace mikserstudio::ui
