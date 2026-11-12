// Hosts one plugin's QML panel in a Quick widget that sees only the plugin panel bridge.
#pragma once

#include <QWidget>

class QLabel;
class QQuickWidget;

namespace mikserstudio::core {
class CommandBus;
class PluginManager;
struct PanelContribution;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class PluginPanelBridge;

// Hosts one plugin QML panel: a QQuickWidget loading <plugin directory>/<qml> with only the
// "mikserstudio" context property (PluginPanelBridge). Load errors are shown in the panel instead.
class PluginPanelHost final : public QWidget {
    Q_OBJECT

public:
    PluginPanelHost(core::CommandBus& bus, const core::PluginManager& plugins, const QString& pluginId,
                    const QString& pluginDirectory, const core::PanelContribution& panel, QWidget* parent = nullptr);

private:
    void showError(const QString& message);

    PluginPanelBridge* m_bridge;
    QQuickWidget* m_quick;
    QLabel* m_error;
};

}  // namespace mikserstudio::ui
