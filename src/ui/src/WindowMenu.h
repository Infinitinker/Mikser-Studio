// The Window menu: opens and closes panels, one checkable action per registered panel.
#pragma once

#include <QHash>
#include <QObject>

class QAction;
class QMenu;

namespace mikserstudio::ui {

class DockHost;

// The Window menu: `leadingActions` (e.g. the command palette), then one checkable action per
// registered panel, sorted by title, checked while the panel is open. Kept in sync with the
// dock host's panelsChanged/panelOpened/panelClosed signals.
class WindowMenu final : public QObject {
    Q_OBJECT

public:
    WindowMenu(DockHost& host, QMenu* menu, QList<QAction*> leadingActions, QObject* parent = nullptr);

    [[nodiscard]] QAction* panelAction(const QString& panelId) const { return m_actions.value(panelId); }

private:
    void rebuild();
    void updateChecks();

    DockHost& m_host;
    QMenu* m_menu;
    QList<QAction*> m_leading;
    QHash<QString, QAction*> m_actions;
};

}  // namespace mikserstudio::ui
