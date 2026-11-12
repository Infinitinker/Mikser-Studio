// The "mikserstudio" object that plugin QML panels use to run commands and read their plugin's settings.
#pragma once

#include <QObject>
#include <QVariantMap>

namespace mikserstudio::core {
class CommandBus;
class PluginManager;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

// The context property "mikserstudio" of plugin QML panels (docs/plugins.md, "Panels (QML contract)"):
//   mikserstudio.execute(commandId, params) -> {ok: true, result} | {ok: false, error: {code, message}}
//       A command with a confirmation asks the user first; "Cancel" gives error code "canceled".
//   mikserstudio.settings   the plugin's settings with defaults applied, updated live
//   mikserstudio.pluginId
class PluginPanelBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)

public:
    PluginPanelBridge(core::CommandBus& bus, const core::PluginManager& plugins, QString pluginId,
                      QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap execute(const QString& commandId, const QVariantMap& params = {});

    [[nodiscard]] QVariantMap settings() const { return m_settings; }
    [[nodiscard]] QString pluginId() const { return m_pluginId; }

Q_SIGNALS:
    void settingsChanged();

private:
    core::CommandBus& m_bus;
    QString m_pluginId;
    QVariantMap m_settings;
};

}  // namespace mikserstudio::ui
