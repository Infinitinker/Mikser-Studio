#pragma once

#include <QObject>

class QActionGroup;
class QMenu;
class QWidget;

namespace mikserstudio::ui {

class CommandStatus;
class KddwDockHost;

// The Workspace menu: switch between workspaces (the current one is checked), "Save Workspace
// As…" and "Reset Workspace". Problems are shown in the status bar.
class WorkspaceMenu final : public QObject {
    Q_OBJECT

public:
    WorkspaceMenu(KddwDockHost& host, QMenu* menu, CommandStatus& status, QWidget* dialogParent,
                  QObject* parent = nullptr);

    void switchTo(const QString& name);
    void saveAs(const QString& name);  // Overwrites a saved workspace of that name.
    void resetCurrent();
    // Refills the menu; runs whenever the menu is about to show.
    void rebuild();

private:
    void askAndSave();

    KddwDockHost& m_host;
    QMenu* m_menu;
    CommandStatus& m_status;
    QWidget* m_dialogParent;
    QActionGroup* m_group = nullptr;
};

}  // namespace mikserstudio::ui
