// The desktop shell: the main window with its menus, panels, command palette and status bar, built on the command bus.
#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

class QAction;
class QLabel;
class QMainWindow;
class QMenu;

namespace mikserstudio::core {
class CommandBus;
class PluginManager;
class ProjectModel;
class Transport;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class ClipSelection;
class CommandLogModel;
class CommandMenus;
class CommandPalette;
class CommandStatus;
class DockHost;
class KddwDockHost;
class ThemeObject;
class TimelineController;
class WindowMenu;
class WorkspaceMenu;

// The desktop shell: the KDDockWidgets main window with
//   - menus generated from the command specs (one per category, kept in sync with the bus;
//     shortcuts from the specs; Undo/Redo named after the edit they revert),
//   - a Window menu listing every registered panel (checked while open) and the command palette
//     (Ctrl+Shift+P / Ctrl+K),
//   - a Workspace menu (switch, save as, reset),
//   - a status bar with the result or error of the last command,
//   - the core panels "mikserstudio.core/media-bin", "timeline", "properties", "ai", "log", "plugins",
//   - one panel "<plugin-id>/<panel-id>" per panel of every active plugin (kept in sync with
//     PluginManager::pluginsChanged).
//
// Construct after applyTheme() and before any other QML engine; register the app's own panels
// (monitors) on dockHost(), then call restoreState() and show(). The last layout is saved to
// <configDir>/layout.json when the main window closes; workspaces live in
// <configDir>/workspaces/. `bus`, `project` and `plugins` must outlive the Shell. Main thread only.
class Shell final : public QObject {
    Q_OBJECT

public:
    Shell(core::CommandBus& bus, core::ProjectModel& project, core::PluginManager& plugins, QString configDir,
          QObject* parent = nullptr);
    ~Shell() override;

    [[nodiscard]] DockHost& dockHost() const;
    [[nodiscard]] QMainWindow* mainWindow() const;
    [[nodiscard]] ClipSelection& selection() const;
    [[nodiscard]] ThemeObject& theme() const;
    [[nodiscard]] CommandPalette& commandPalette() const;

    // The playhead of the timeline follows this transport (null: the playhead stays at 0).
    void setProgramTransport(core::Transport* transport);

    // Restores the last session's layout at once, or else the last used workspace (or "Editing"),
    // which is applied when the window is shown so it is laid out for the window's real size.
    void restoreState();
    // Saves the current layout and workspace name for the next start. Returns false on failure.
    bool saveState();
    void show();
    void showCommandPalette();
    // Opens the Settings window, at `section` (such as "AI tools") when there is one.
    void showSettings(const QString& section = {});

    [[nodiscard]] QMenu* windowMenu() const { return m_windowMenu; }
    [[nodiscard]] QMenu* workspaceMenu() const { return m_workspaceMenu; }
    [[nodiscard]] WorkspaceMenu& workspaces() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void registerCorePanels();
    void syncPluginPanels();
    void buildMenus();
    void updateWorkspaceLabel();
    void updateWindowTitle();
    void applyPendingWorkspace();

    core::CommandBus& m_bus;
    core::ProjectModel& m_project;
    core::PluginManager& m_plugins;
    QString m_configDir;

    std::unique_ptr<ThemeObject> m_theme;
    std::unique_ptr<ClipSelection> m_selection;
    std::unique_ptr<CommandLogModel> m_log;
    std::unique_ptr<TimelineController> m_timeline;
    QSet<QString> m_pluginPanels;  // Registered "<plugin-id>/<panel-id>" ids.
    QString m_pendingWorkspace;  // Applied by show().
    bool m_restoreMaximized = false;  // From shell.json: the layout keeps only one window state.

    QMenu* m_windowMenu = nullptr;
    QMenu* m_workspaceMenu = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_workspaceLabel = nullptr;
    CommandStatus* m_status = nullptr;
    CommandMenus* m_commandMenus = nullptr;
    WindowMenu* m_windowMenuController = nullptr;
    WorkspaceMenu* m_workspaceMenuController = nullptr;
    CommandPalette* m_palette = nullptr;

    // Destroyed first: the panels it owns reference everything above.
    std::unique_ptr<KddwDockHost> m_dockHost;
};

}  // namespace mikserstudio::ui
