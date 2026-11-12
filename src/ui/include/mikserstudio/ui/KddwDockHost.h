// The DockHost built on KDDockWidgets: dockable, tabbed and floating panels, and workspaces saved as layout files.
#pragma once

#include "mikserstudio/ui/DockHost.h"

#include <QHash>
#include <QList>
#include <QPointer>

#include <memory>

namespace KDDockWidgets {
namespace Core {
class DockWidget;
}
namespace QtWidgets {
class DockWidget;
class MainWindow;
}  // namespace QtWidgets
}  // namespace KDDockWidgets

namespace mikserstudio::ui {

struct WorkspacePreset;

// DockHost on KDDockWidgets 2.5 (QtWidgets frontend).
//
// Panels live in KDDockWidgets dock widgets named after the panel id ("<id>" for the first
// instance, "<id>#<n>" for further instances of non-unique panels), created when first opened
// and kept when closed so they reopen where they were. Workspaces are KDDockWidgets LayoutSaver
// JSON files in the workspace directory. The built-in workspaces "Editing", "Color" and "AI" are
// defined in code and used when no file with their name has been saved.
//
// KDDockWidgets keeps global state (its dock registry and factory), so only one host may exist
// at a time. Needs a QApplication.
class KddwDockHost final : public DockHost {
    Q_OBJECT

public:
    explicit KddwDockHost(QString workspaceDirectory, QObject* parent = nullptr);
    ~KddwDockHost() override;

    [[nodiscard]] QWidget* mainWindow() const override;

    // Registering an id again replaces its descriptor. Open placeholders of this id get the real
    // panel at once (e.g. when a plugin is activated after the layout was restored).
    void registerPanel(PanelDescriptor descriptor) override;
    // Closes the open instances and replaces their content with placeholders.
    void unregisterPanel(const QString& id) override;
    [[nodiscard]] QList<PanelDescriptor> panels() const override;  // Sorted by id.

    std::expected<void, QString> openPanel(const QString& id) override;
    void closePanel(const QString& id) override;  // Closes every instance.
    [[nodiscard]] bool isPanelOpen(const QString& id) const override;

    [[nodiscard]] QByteArray saveLayout() const override;
    // Restores the main window geometry too; used for the layout of the last session.
    std::expected<void, QString> restoreLayout(const QByteArray& layout) override;

    // Built-in workspaces first (in their order), then saved ones sorted by name.
    [[nodiscard]] QStringList workspaces() const override;
    // A built-in name in another case ("editing") saves that built-in workspace ("Editing").
    std::expected<void, QString> saveWorkspace(const QString& name) override;
    // Keeps the main window geometry. A saved file wins over a built-in workspace of that name.
    std::expected<void, QString> loadWorkspace(const QString& name) override;

    [[nodiscard]] QStringList builtInWorkspaces() const override;
    [[nodiscard]] bool hasSavedWorkspace(const QString& name) const override;
    std::expected<void, QString> resetWorkspace(const QString& name) override;
    [[nodiscard]] QString currentWorkspace() const override { return m_currentWorkspace; }
    void setCurrentWorkspace(const QString& name) override;
    [[nodiscard]] QString workspaceDirectory() const { return m_workspaceDirectory; }

    std::expected<void, QString> setPanelFloating(const QString& id, bool floating) override;
    [[nodiscard]] bool isPanelFloating(const QString& id) const;
    [[nodiscard]] QWidget* panelWindow(const QString& id) const override;
    // The content widget of the first instance (null when the panel was never opened).
    [[nodiscard]] QWidget* panelWidget(const QString& id) const;
    [[nodiscard]] bool isPlaceholder(const QString& id) const;
    [[nodiscard]] QStringList openPanels() const;  // Panel ids, sorted.

private:
    using Dock = KDDockWidgets::QtWidgets::DockWidget;

    static KDDockWidgets::Core::DockWidget* createDockForLayout(const QString& name);

    [[nodiscard]] const PanelDescriptor* descriptor(const QString& id) const;
    [[nodiscard]] QList<Dock*> docksOf(const QString& id) const;
    [[nodiscard]] Dock* firstDock(const QString& id) const;
    Dock* createDock(const QString& name);
    [[nodiscard]] QWidget* createContent(const QString& id, Dock* dock);
    void setContent(Dock* dock, QWidget* content);
    void placeInDefaultArea(Dock* dock, DockArea area);
    void applyPreset(const WorkspacePreset& preset);
    void closeAll();
    std::expected<void, QString> restore(const QByteArray& layout, bool keepMainWindowGeometry);
    // The file of a workspace; built-in names in any case map to the built-in workspace's file.
    [[nodiscard]] QString workspacePath(const QString& name) const;

    static KddwDockHost* s_instance;

    QString m_workspaceDirectory;
    QString m_currentWorkspace;
    std::unique_ptr<KDDockWidgets::QtWidgets::MainWindow> m_mainWindow;
    QHash<QString, PanelDescriptor> m_panels;
    QHash<QString, QPointer<Dock>> m_docks;  // By dock name.
};

}  // namespace mikserstudio::ui
