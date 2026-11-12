#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

class QAction;
class QMenu;
class QMenuBar;
class QWidget;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Generates one menu per command category in the menu bar and keeps it in sync with the bus:
// commands registered later get an action, unregistered ones lose theirs, empty menus go away.
// Actions carry the spec's title (with "…" when params are asked for), description and
// shortcut; "Undo"/"Redo" show the text of the edit they would revert and are disabled when there
// is nothing to undo or redo. Category menus come in the order File, Project, Edit, Timeline,
// then the others alphabetically, all before `insertBefore` (e.g. the Workspace menu).
class CommandMenus final : public QObject {
    Q_OBJECT

public:
    CommandMenus(core::CommandBus& bus, QMenuBar* menuBar, QWidget* dialogParent, QObject* parent = nullptr);

    void setInsertBefore(QAction* menuAction);
    [[nodiscard]] QStringList categories() const;  // In menu bar order.
    [[nodiscard]] QMenu* menu(const QString& category) const;
    [[nodiscard]] QAction* action(const QString& commandId) const;

private:
    void add(const QString& id);
    void remove(const QString& id);
    void updateUndoRedo();
    QMenu* ensureMenu(const QString& category);

    core::CommandBus& m_bus;
    QMenuBar* m_menuBar;
    QWidget* m_dialogParent;
    QAction* m_insertBefore = nullptr;
    QHash<QString, QMenu*> m_menus;  // By category.
    QHash<QString, QAction*> m_actions;  // By command id.
    QHash<QString, QString> m_categoryOf;
};

}  // namespace mikserstudio::ui
