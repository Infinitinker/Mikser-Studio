#include "CommandMenus.h"

#include "CommandInvoker.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

const QString kUndo = u"edit.undo"_s;
const QString kRedo = u"edit.redo"_s;

int categoryRank(const QString& category)
{
    static const QStringList preferred{u"File"_s, u"Project"_s, u"Edit"_s, u"Timeline"_s};
    const qsizetype index = preferred.indexOf(category);
    return index < 0 ? static_cast<int>(preferred.size()) : static_cast<int>(index);
}

bool categoryLess(const QString& a, const QString& b)
{
    const int rankA = categoryRank(a);
    const int rankB = categoryRank(b);
    if (rankA != rankB)
        return rankA < rankB;
    return a.localeAwareCompare(b) < 0;
}

// Undo and Redo lead the Edit menu; everything else is sorted by title.
std::pair<int, QString> actionKey(const QAction* action)
{
    const QString id = action->data().toString();
    const int rank = id == kUndo ? 0 : id == kRedo ? 1 : 2;
    return {rank, action->text().toLower()};
}

}  // namespace

CommandMenus::CommandMenus(core::CommandBus& bus, QMenuBar* menuBar, QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_menuBar(menuBar)
    , m_dialogParent(dialogParent)
{
    for (const core::CommandSpec& spec : bus.specs())
        add(spec.id);
    connect(&bus, &core::CommandBus::commandRegistered, this, &CommandMenus::add);
    connect(&bus, &core::CommandBus::commandUnregistered, this, &CommandMenus::remove);
    connect(&bus.undoStack(), &core::UndoStack::changed, this, &CommandMenus::updateUndoRedo);
    updateUndoRedo();
}

void CommandMenus::setInsertBefore(QAction* menuAction)
{
    m_insertBefore = menuAction;
    // Move the existing category menus in front of it, keeping their order.
    for (const QString& category : categories()) {
        QAction* action = m_menus.value(category)->menuAction();
        m_menuBar->removeAction(action);
        m_menuBar->insertAction(m_insertBefore, action);
    }
}

QStringList CommandMenus::categories() const
{
    QStringList list = m_menus.keys();
    std::ranges::sort(list, categoryLess);
    return list;
}

QMenu* CommandMenus::menu(const QString& category) const
{
    return m_menus.value(category);
}

QAction* CommandMenus::action(const QString& commandId) const
{
    return m_actions.value(commandId);
}

void CommandMenus::add(const QString& id)
{
    const core::CommandSpec* spec = m_bus.spec(id);
    if (!spec || m_actions.contains(id))
        return;
    const QString category = spec->category.isEmpty() ? u"Commands"_s : spec->category;
    QMenu* target = ensureMenu(category);

    auto* action = new QAction(needsParamsDialog(*spec) ? spec->title + u"…"_s : spec->title, target);
    action->setData(id);
    action->setToolTip(spec->description);
    action->setStatusTip(spec->description);
    if (!spec->shortcut.isEmpty()) {
        action->setShortcut(QKeySequence(spec->shortcut, QKeySequence::PortableText));
        // Floating panels are separate windows; shortcuts must work there too.
        action->setShortcutContext(Qt::ApplicationShortcut);
    }
    connect(action, &QAction::triggered, this, [this, id] { invokeCommand(m_bus, id, m_dialogParent); });

    QAction* before = nullptr;
    const auto key = actionKey(action);
    for (QAction* existing : target->actions()) {
        if (key < actionKey(existing)) {
            before = existing;
            break;
        }
    }
    target->insertAction(before, action);
    m_actions.insert(id, action);
    m_categoryOf.insert(id, category);
    if (id == kUndo || id == kRedo)
        updateUndoRedo();
}

void CommandMenus::remove(const QString& id)
{
    QAction* action = m_actions.take(id);
    if (!action)
        return;
    const QString category = m_categoryOf.take(id);
    QMenu* menu = m_menus.value(category);
    delete action;
    if (menu && menu->actions().isEmpty()) {
        m_menus.remove(category);
        m_menuBar->removeAction(menu->menuAction());
        menu->deleteLater();
    }
}

void CommandMenus::updateUndoRedo()
{
    const core::UndoStack& undo = m_bus.undoStack();
    if (QAction* action = m_actions.value(kUndo)) {
        action->setText(undo.canUndo() ? u"Undo %1"_s.arg(undo.undoText()) : u"Undo"_s);
        action->setEnabled(undo.canUndo());
    }
    if (QAction* action = m_actions.value(kRedo)) {
        action->setText(undo.canRedo() ? u"Redo %1"_s.arg(undo.redoText()) : u"Redo"_s);
        action->setEnabled(undo.canRedo());
    }
}

QMenu* CommandMenus::ensureMenu(const QString& category)
{
    if (QMenu* existing = m_menus.value(category))
        return existing;
    auto* menu = new QMenu(category, m_menuBar);
    QAction* before = m_insertBefore;
    for (const QString& other : categories()) {
        if (categoryLess(category, other)) {
            before = m_menus.value(other)->menuAction();
            break;
        }
    }
    m_menuBar->insertMenu(before, menu);
    m_menus.insert(category, menu);
    return menu;
}

}  // namespace mikserstudio::ui
