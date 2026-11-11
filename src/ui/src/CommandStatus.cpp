#include "CommandStatus.h"

#include "CommandText.h"
#include "QuietCommands.h"

#include "mikserstudio/core/CommandBus.h"

#include <QLabel>
#include <QStyle>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr qsizetype kSummaryChars = 100;

}  // namespace

CommandStatus::CommandStatus(core::CommandBus& bus, QLabel* label, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_label(label)
{
    m_label->setProperty("role", u"status"_s);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connect(&bus, &core::CommandBus::executed, this,
            [this](const QString& id, const QJsonObject&, const QJsonObject& result) {
                if (isQuietCommand(m_bus, id))
                    return;
                // Only human-readable results; ids and data are in the log.
                const QString text = result.value(u"text").toString(result.value(u"message").toString());
                showMessage(text.isEmpty() ? u"%1 — done"_s.arg(commandTitle(m_bus, id))
                                           : u"%1 — %2"_s.arg(commandTitle(m_bus, id), text.left(kSummaryChars)),
                            false);
            });
    connect(&bus, &core::CommandBus::failed, this,
            [this](const QString& id, const QJsonObject&, const core::CommandError& error) {
                if (isQuietCommand(m_bus, id))
                    return;
                showMessage(u"%1 failed: %2"_s.arg(commandTitle(m_bus, id), error.message), true);
            });
}

void CommandStatus::showMessage(const QString& text, bool error)
{
    m_label->setText(text);
    m_label->setToolTip(text);
    m_label->setProperty("state", error ? u"error"_s : u"ok"_s);
    m_label->style()->unpolish(m_label);
    m_label->style()->polish(m_label);
}

QString CommandStatus::text() const
{
    return m_label->text();
}

}  // namespace mikserstudio::ui
