// The desktop shell: wires the menus, panels, command palette, status bar and workspaces into the main
// window.
#include "mikserstudio/ui/Shell.h"

#include "AiPanel.h"
#include "CommandLogModel.h"
#include "CommandMenus.h"
#include "CommandStatus.h"
#include "LogPanel.h"
#include "Logging.h"
#include "MediaBinPanel.h"
#include "PluginPanelHost.h"
#include "PluginsPanel.h"
#include "PropertiesPanel.h"
#include "TimelineController.h"
#include "TimelineView.h"
#include "WindowMenu.h"
#include "WorkspaceMenu.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/ClipSelection.h"
#include "mikserstudio/ui/CommandPalette.h"
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Theme.h"

#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QSaveFile>
#include <QStatusBar>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr QSize kDefaultWindowSize(1600, 960);
const auto kLayoutFile = u"layout.json"_s;
const auto kShellFile = u"shell.json"_s;

DockArea toDockArea(const QString& area)
{
    if (area == u"left")
        return DockArea::Left;
    if (area == u"bottom")
        return DockArea::Bottom;
    if (area == u"center")
        return DockArea::Center;
    if (area == u"floating")
        return DockArea::Floating;
    return DockArea::Right;
}

}  // namespace

Shell::Shell(core::CommandBus& bus, core::ProjectModel& project, core::PluginManager& plugins, QString configDir,
             QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_project(project)
    , m_plugins(plugins)
    , m_configDir(std::move(configDir))
    , m_theme(std::make_unique<ThemeObject>())
    , m_selection(std::make_unique<ClipSelection>(project))
    , m_log(std::make_unique<CommandLogModel>(bus))
    , m_timeline(std::make_unique<TimelineController>(bus, project, *m_selection))
    , m_dockHost(std::make_unique<KddwDockHost>(QDir(m_configDir).filePath(u"workspaces"_s)))
{
    QMainWindow* window = mainWindow();
    window->resize(kDefaultWindowSize);
    window->installEventFilter(this);

    m_statusLabel = new QLabel(window);
    m_workspaceLabel = new QLabel(window);
    m_workspaceLabel->setProperty("role", u"status"_s);
    window->statusBar()->addWidget(m_statusLabel, 1);
    window->statusBar()->addPermanentWidget(m_workspaceLabel);
    window->statusBar()->setSizeGripEnabled(false);
    m_status = new CommandStatus(bus, m_statusLabel, this);
    m_status->showMessage(u"Ready"_s, false);

    m_palette = new CommandPalette(bus, window);
    registerCorePanels();
    buildMenus();
    connect(&plugins, &core::PluginManager::pluginsChanged, this, &Shell::syncPluginPanels);
    syncPluginPanels();
    connect(m_dockHost.get(), &KddwDockHost::currentWorkspaceChanged, this, &Shell::updateWorkspaceLabel);
    updateWorkspaceLabel();
    connect(&project, &core::ProjectModel::filePathChanged, this, &Shell::updateWindowTitle);
    connect(&bus.undoStack(), &core::UndoStack::cleanChanged, this, &Shell::updateWindowTitle);
    updateWindowTitle();
}

Shell::~Shell() = default;

DockHost& Shell::dockHost() const
{
    return *m_dockHost;
}

QMainWindow* Shell::mainWindow() const
{
    return qobject_cast<QMainWindow*>(m_dockHost->mainWindow());
}

ClipSelection& Shell::selection() const
{
    return *m_selection;
}

ThemeObject& Shell::theme() const
{
    return *m_theme;
}

CommandPalette& Shell::commandPalette() const
{
    return *m_palette;
}

void Shell::setProgramTransport(core::Transport* transport)
{
    m_timeline->setTransport(transport);
}

void Shell::restoreState()
{
    QString workspace = m_dockHost->builtInWorkspaces().constFirst();
    QJsonObject shellState;
    QFile shellFile(QDir(m_configDir).filePath(kShellFile));
    if (shellFile.open(QIODevice::ReadOnly)) {
        shellState = QJsonDocument::fromJson(shellFile.readAll()).object();
        const QString saved = shellState.value(u"workspace").toString();
        if (!saved.isEmpty() && m_dockHost->workspaces().contains(saved, Qt::CaseInsensitive))
            workspace = saved;
    }

    QFile layoutFile(QDir(m_configDir).filePath(kLayoutFile));
    if (layoutFile.open(QIODevice::ReadOnly)) {
        const auto restored = m_dockHost->restoreLayout(layoutFile.readAll());
        if (restored) {
            // The layout is the workspace as the user left it.
            m_dockHost->setCurrentWorkspace(workspace);
            m_restoreMaximized = shellState.value(u"maximized").toBool();
            qCInfo(lcShell) << "Restored the last layout";
            return;
        }
        qCWarning(lcShell).noquote() << restored.error();
        m_status->showMessage(
            u"The panels could not be arranged as you left them, so the “%1” workspace is shown."_s.arg(workspace),
            true);
    }
    // Workspaces are laid out in proportion to the window, so wait until it has its real size.
    m_pendingWorkspace = std::move(workspace);
    if (mainWindow()->isVisible())
        applyPendingWorkspace();
}

bool Shell::saveState()
{
    if (!QDir().mkpath(m_configDir)) {
        qCWarning(lcShell) << "Cannot create the config folder" << m_configDir;
        return false;
    }
    QSaveFile layout(QDir(m_configDir).filePath(kLayoutFile));
    QSaveFile shell(QDir(m_configDir).filePath(kShellFile));
    // QWidget keeps "maximized" while the window is minimized; the layout saves only "minimized".
    const QJsonObject state{{u"workspace"_s, m_dockHost->currentWorkspace()},
                            {u"maximized"_s, mainWindow()->isMaximized()}};
    const bool ok = layout.open(QIODevice::WriteOnly) && layout.write(m_dockHost->saveLayout()) >= 0 &&
                    layout.commit() && shell.open(QIODevice::WriteOnly) &&
                    shell.write(QJsonDocument(state).toJson()) >= 0 && shell.commit();
    if (!ok)
        qCWarning(lcShell) << "Could not save the window layout to" << m_configDir;
    return ok;
}

void Shell::show()
{
    // A pending workspace means there is no saved layout (first start): fill the screen's work
    // area instead of guessing a size that may run under the taskbar. After a restore, use the
    // saved "maximized" and never start minimized: KDDockWidgets saves one window state (minimized
    // wins over maximized) and applies it to the native window, where plain show() would keep it.
    if (!m_pendingWorkspace.isEmpty() || m_restoreMaximized)
        mainWindow()->showMaximized();
    else
        mainWindow()->showNormal();
    applyPendingWorkspace();
}

void Shell::applyPendingWorkspace()
{
    if (m_pendingWorkspace.isEmpty())
        return;
    const QString workspace = std::exchange(m_pendingWorkspace, QString());
    if (const auto loaded = m_dockHost->loadWorkspace(workspace); !loaded)
        m_status->showMessage(loaded.error(), true);
}

void Shell::showCommandPalette()
{
    m_palette->popup();
}

WorkspaceMenu& Shell::workspaces() const
{
    return *m_workspaceMenuController;
}

bool Shell::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == mainWindow() && event->type() == QEvent::Close)
        saveState();
    return QObject::eventFilter(watched, event);
}

void Shell::registerCorePanels()
{
    using namespace panel_ids;
    KddwDockHost& host = *m_dockHost;
    host.registerPanel({kMediaBin.toString(), u"Media bin"_s, DockArea::Left, true,
                        [this](QWidget* parent) { return new MediaBinPanel(m_bus, m_project, parent); }});
    host.registerPanel({kTimeline.toString(), u"Timeline"_s, DockArea::Bottom, true,
                        [this](QWidget* parent) { return new TimelineView(*m_timeline, *m_theme, parent); }});
    host.registerPanel({kProperties.toString(), u"Properties"_s, DockArea::Right, true, [this](QWidget* parent) {
                            return new PropertiesPanel(m_bus, m_project, *m_selection, parent);
                        }});
    host.registerPanel({kAi.toString(), u"AI"_s, DockArea::Right, true,
                        [this](QWidget* parent) { return new AiPanel(m_bus, parent); }});
    host.registerPanel({kLog.toString(), u"Log"_s, DockArea::Bottom, true,
                        [this](QWidget* parent) { return new LogPanel(*m_log, parent); }});
    host.registerPanel({kPlugins.toString(), u"Plugins"_s, DockArea::Right, true,
                        [this](QWidget* parent) { return new PluginsPanel(m_plugins, parent); }});
}

void Shell::syncPluginPanels()
{
    QSet<QString> wanted;
    for (const core::PluginState& state : m_plugins.plugins()) {
        if (state.status != core::PluginStatus::Active)
            continue;
        for (const core::PanelContribution& panel : state.manifest.panels) {
            const QString id = state.manifest.id + u'/' + panel.id;
            wanted.insert(id);
            if (m_pluginPanels.contains(id))
                continue;
            const QString pluginId = state.manifest.id;
            const QString directory = state.manifest.directory;
            m_dockHost->registerPanel({id, panel.title, toDockArea(panel.defaultArea), true,
                                       [this, pluginId, directory, panel](QWidget* parent) {
                                           return new PluginPanelHost(m_bus, m_plugins, pluginId, directory, panel,
                                                                      parent);
                                       }});
            m_pluginPanels.insert(id);
        }
    }
    const QSet<QString> registered = m_pluginPanels;
    for (const QString& id : registered) {
        if (wanted.contains(id))
            continue;
        m_dockHost->unregisterPanel(id);
        m_pluginPanels.remove(id);
    }
}

void Shell::buildMenus()
{
    QMainWindow* window = mainWindow();
    QMenuBar* bar = window->menuBar();
    m_workspaceMenu = bar->addMenu(u"&Workspace"_s);
    m_windowMenu = bar->addMenu(u"W&indow"_s);

    m_commandMenus = new CommandMenus(m_bus, bar, window, this);
    m_commandMenus->setInsertBefore(m_workspaceMenu->menuAction());

    auto* palette = new QAction(u"Command palette…"_s, window);
    palette->setShortcuts({QKeySequence(u"Ctrl+Shift+P"_s), QKeySequence(u"Ctrl+K"_s)});
    palette->setShortcutContext(Qt::ApplicationShortcut);
    palette->setToolTip(u"Search and run any command."_s);
    connect(palette, &QAction::triggered, this, &Shell::showCommandPalette);
    window->addAction(palette);
    m_windowMenuController = new WindowMenu(*m_dockHost, m_windowMenu, *m_status, {palette}, this);

    m_workspaceMenuController = new WorkspaceMenu(*m_dockHost, m_workspaceMenu, *m_status, window, this);
}

void Shell::updateWorkspaceLabel()
{
    const QString name = m_dockHost->currentWorkspace();
    m_workspaceLabel->setText(name.isEmpty() ? QString() : u"Workspace: %1"_s.arg(name));
}

void Shell::updateWindowTitle()
{
    // "Cut v1.2* – Mikser Studio Dev": the project's file name, marked while there are unsaved changes
    // ([*] is where Qt shows the mark), and the instance's name (core/Instance.h).
    const QString file = m_project.filePath();
    const QString name = file.isEmpty() ? u"Untitled"_s : QFileInfo(file).completeBaseName();
    QMainWindow* window = mainWindow();
    window->setWindowTitle(u"%1[*] – %2"_s.arg(name, QCoreApplication::applicationName()));
    window->setWindowModified(!m_bus.undoStack().isClean());
}

}  // namespace mikserstudio::ui
