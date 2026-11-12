// The docking seam of the UI: panels, areas and workspaces behind one interface, so the docking library can be
// replaced.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

class QWidget;

namespace mikserstudio::ui {

enum class DockArea { Left, Right, Bottom, Center, Floating };

// A kind of panel the user can open. Panels are created on demand by `create` and identified
// by a stable "<owner>/<panel>" id (e.g. "mikserstudio.core/monitor.program",
// "mikserstudio.examples.hello/greeter"), which is what saved workspaces store.
struct PanelDescriptor {
    QString id;
    QString title;
    DockArea defaultArea = DockArea::Center;
    bool unique = true;  // Only one instance can be open.
    std::function<QWidget*(QWidget* parent)> create;
};

// The docking layer, kept behind this interface so the docking library (and its license) can be
// swapped: KDDockWidgets today, Qt Advanced Docking System as the fallback.
//
// Every panel can be docked, tabbed, floated on any monitor and closed. A workspace is a named,
// saved layout. Restoring a workspace that mentions a panel nobody registered (e.g. a removed
// plugin) shows a placeholder panel instead of failing, so workspaces survive plugin removal.
class DockHost : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~DockHost() override = default;

    [[nodiscard]] virtual QWidget* mainWindow() const = 0;

    virtual void registerPanel(PanelDescriptor descriptor) = 0;
    virtual void unregisterPanel(const QString& id) = 0;  // Closes open instances.
    [[nodiscard]] virtual QList<PanelDescriptor> panels() const = 0;

    // Opens (or raises, for unique panels) a panel in its default area.
    virtual std::expected<void, QString> openPanel(const QString& id) = 0;
    virtual void closePanel(const QString& id) = 0;
    [[nodiscard]] virtual bool isPanelOpen(const QString& id) const = 0;

    [[nodiscard]] virtual QByteArray saveLayout() const = 0;
    virtual std::expected<void, QString> restoreLayout(const QByteArray& layout) = 0;

    // Named workspaces stored as files in the workspace directory.
    [[nodiscard]] virtual QStringList workspaces() const = 0;
    virtual std::expected<void, QString> saveWorkspace(const QString& name) = 0;
    virtual std::expected<void, QString> loadWorkspace(const QString& name) = 0;

Q_SIGNALS:
    void panelOpened(const QString& id);
    void panelClosed(const QString& id);
    void panelsChanged();  // A panel was registered or unregistered.
    void layoutChanged();
};

}  // namespace mikserstudio::ui
