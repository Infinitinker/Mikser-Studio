#include "ParamsDialog.h"

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

    m_form = new SchemaForm(spec.paramsSchema, {}, this);
    m_error = new QLabel(this);
    m_error->setProperty("role", u"error"_s);
    m_error->setWordWrap(true);
    m_error->hide();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* runButton = buttons->addButton(u"Run"_s, QDialogButtonBox::AcceptRole);
    runButton->setProperty("role", u"primary"_s);
    runButton->setDefault(true);
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
    m_result = m_bus.execute(m_commandId, m_form->value());
    if (m_result) {
        accept();
        return;
    }
    m_error->setText(m_result.error().message);
    m_error->show();
}

}  // namespace mikserstudio::ui
