#pragma once

#include <QObject>

class QLabel;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Shows the result of the last command (or its error, in the danger color) in a status bar
// label. Quiet commands (read-only ones and UI bookkeeping, see QuietCommands.h) are not shown.
// Other code can post a message of its own with showMessage().
class CommandStatus final : public QObject {
    Q_OBJECT

public:
    CommandStatus(core::CommandBus& bus, QLabel* label, QObject* parent = nullptr);

    void showMessage(const QString& text, bool error);
    [[nodiscard]] QString text() const;

private:
    const core::CommandBus& m_bus;
    QLabel* m_label;
};

}  // namespace mikserstudio::ui
