#include "DemoWindow.h"

#include "mikserstudio/engine/MonitorPanel.h"

#include <QDockWidget>
#include <QLabel>

namespace mikserstudio::demo {

DemoWindow::DemoWindow(core::Transport& transport, const QString& title)
    : m_dock(new QDockWidget(title, this))
    , m_panel(new engine::MonitorPanel(transport, m_dock))
{
    setWindowTitle(tr("Mikser Studio monitor demo"));
    auto* placeholder = new QLabel(tr("Drag the monitor's title bar to float it; double-click it to dock it again."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    setCentralWidget(placeholder);

    m_dock->setObjectName(QStringLiteral("monitorDock"));
    m_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_dock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);
    resizeDocks({m_dock}, {900}, Qt::Horizontal);
    resize(1280, 760);
}

}  // namespace mikserstudio::demo
