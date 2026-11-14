// The Window menu's actions, kept in sync with the dock host's panels.
#include "WindowMenu.h"

#include "CommandStatus.h"

#include "mikserstudio/ui/DockHost.h"

#include <QAction>
#include <QMenu>

#include <algorithm>

namespace mikserstudio::ui {

WindowMenu::WindowMenu(DockHost& host, QMenu* menu, CommandStatus& status, QList<QAction*> leadingActions,
                       QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_menu(menu)
    , m_status(status)
    , m_leading(std::move(leadingActions))
{
    connect(&host, &DockHost::panelsChanged, this, &WindowMenu::rebuild);
    connect(&host, &DockHost::panelOpened, this, &WindowMenu::updateChecks);
    connect(&host, &DockHost::panelClosed, this, &WindowMenu::updateChecks);
    connect(&host, &DockHost::layoutChanged, this, &WindowMenu::updateChecks);
    connect(menu, &QMenu::aboutToShow, this, &WindowMenu::updateChecks);
    rebuild();
}

void WindowMenu::rebuild()
{
    for (QAction* action : std::as_const(m_actions))
        delete action;
    m_actions.clear();
    m_menu->clear();
    for (QAction* action : std::as_const(m_leading))
        m_menu->addAction(action);
    if (!m_leading.isEmpty())
        m_menu->addSeparator();

    QList<PanelDescriptor> panels = m_host.panels();
    std::ranges::sort(panels, [](const PanelDescriptor& a, const PanelDescriptor& b) {
        return a.title.localeAwareCompare(b.title) < 0;
    });
    for (const PanelDescriptor& panel : panels) {
        auto* action = m_menu->addAction(panel.title);
        action->setCheckable(true);
        action->setToolTip(panel.id);
        action->setData(panel.id);
        const QString id = panel.id;
        connect(action, &QAction::triggered, this, [this, id](bool checked) {
            if (checked) {
                if (const auto opened = m_host.openPanel(id); !opened)
                    m_status.showMessage(opened.error(), true);
            } else {
                m_host.closePanel(id);
            }
            updateChecks();
        });
        m_actions.insert(id, action);
    }
    updateChecks();
}

void WindowMenu::updateChecks()
{
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it) {
        const QSignalBlocker blocker(it.value());
        it.value()->setChecked(m_host.isPanelOpen(it.key()));
    }
}

}  // namespace mikserstudio::ui
