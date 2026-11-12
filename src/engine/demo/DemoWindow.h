// The engine demo's main window: one monitor panel in a dock that can be floated and docked again.
#pragma once

#include <QMainWindow>

class QDockWidget;

namespace mikserstudio::core {
class Transport;
}

namespace mikserstudio::engine {
class MonitorPanel;
}

namespace mikserstudio::demo {

// A main window with one dockable, floatable monitor panel for `transport`.
class DemoWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit DemoWindow(core::Transport& transport, const QString& title);

    [[nodiscard]] QDockWidget* monitorDock() const { return m_dock; }
    [[nodiscard]] engine::MonitorPanel* monitorPanel() const { return m_panel; }

private:
    QDockWidget* m_dock = nullptr;
    engine::MonitorPanel* m_panel = nullptr;
};

}  // namespace mikserstudio::demo
