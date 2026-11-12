#pragma once

#include <QWidget>

class QTreeView;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

class CommandLogModel;

// "Log" panel: the commands run in this session with their params and results or errors.
class LogPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LogPanel(CommandLogModel& model, QWidget* parent = nullptr);

private:
    QTreeView* m_view;
};

}  // namespace mikserstudio::ui
