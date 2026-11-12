// The Workspace menu's actions and the prompts for saving a workspace.
#include "WorkspaceMenu.h"

#include "CommandStatus.h"

#include "mikserstudio/ui/KddwDockHost.h"

#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

WorkspaceMenu::WorkspaceMenu(KddwDockHost& host, QMenu* menu, CommandStatus& status, QWidget* dialogParent,
                             QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_menu(menu)
    , m_status(status)
    , m_dialogParent(dialogParent)
{
    connect(menu, &QMenu::aboutToShow, this, &WorkspaceMenu::rebuild);
    rebuild();
}

void WorkspaceMenu::switchTo(const QString& name)
{
    if (const auto loaded = m_host.loadWorkspace(name); !loaded) {
        m_status.showMessage(loaded.error(), true);
        return;
    }
    m_status.showMessage(u"Workspace “%1”"_s.arg(m_host.currentWorkspace()), false);
}

void WorkspaceMenu::saveAs(const QString& name)
{
    if (const auto saved = m_host.saveWorkspace(name); !saved) {
        m_status.showMessage(saved.error(), true);
        return;
    }
    m_status.showMessage(u"Saved workspace “%1”"_s.arg(name), false);
}

void WorkspaceMenu::resetCurrent()
{
    const QString name = m_host.currentWorkspace().isEmpty() ? KddwDockHost::builtInWorkspaces().constFirst()
                                                             : m_host.currentWorkspace();
    if (const auto reset = m_host.resetWorkspace(name); !reset) {
        m_status.showMessage(reset.error(), true);
        return;
    }
    m_status.showMessage(u"Reset workspace “%1”"_s.arg(name), false);
}

void WorkspaceMenu::rebuild()
{
    m_menu->clear();
    delete m_group;
    m_group = new QActionGroup(m_menu);
    const QStringList builtIn = KddwDockHost::builtInWorkspaces();
    bool separated = false;
    for (const QString& name : m_host.workspaces()) {
        if (!separated && !builtIn.contains(name)) {
            m_menu->addSeparator();
            separated = true;
        }
        QAction* action = m_menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name.compare(m_host.currentWorkspace(), Qt::CaseInsensitive) == 0);
        action->setActionGroup(m_group);
        connect(action, &QAction::triggered, this, [this, name] { switchTo(name); });
    }
    m_menu->addSeparator();
    connect(m_menu->addAction(u"Save workspace as…"_s), &QAction::triggered, this, &WorkspaceMenu::askAndSave);
    QAction* reset = m_menu->addAction(u"Reset workspace"_s);
    reset->setToolTip(u"Brings the current workspace back to its saved or built-in layout."_s);
    connect(reset, &QAction::triggered, this, &WorkspaceMenu::resetCurrent);
}

void WorkspaceMenu::askAndSave()
{
    bool ok = false;
    const QString name = QInputDialog::getText(m_dialogParent, u"Save workspace"_s, u"Workspace name:"_s,
                                               QLineEdit::Normal, m_host.currentWorkspace(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (m_host.hasSavedWorkspace(name) &&
        QMessageBox::question(m_dialogParent, u"Save workspace"_s,
                              u"A workspace named “%1” exists. Replace it?"_s.arg(name)) != QMessageBox::Yes) {
        return;
    }
    saveAs(name);
}

}  // namespace mikserstudio::ui
