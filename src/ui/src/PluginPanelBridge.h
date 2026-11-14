// The "mikserstudio" object that plugin QML panels use to run commands and read their plugin's settings.
#pragma once

#include <QJSValue>
#include <QObject>
#include <QVariantMap>

class QJSEngine;

namespace mikserstudio::core {
class CommandBus;
class PluginManager;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

// The context property "mikserstudio" of plugin QML panels (docs/plugins.md, "Panels (QML contract)"):
//   mikserstudio.execute(commandId, params, callback)
//       Runs the command, which may answer later, and calls callback(reply) once with
//       {ok: true, result} | {ok: false, error: {code, message}}. A command with a confirmation
//       asks the user first; "Cancel" gives error code "canceled".
//   mikserstudio.settings   the plugin's settings with defaults applied, updated live
//   mikserstudio.pluginId
class PluginPanelBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)

public:
    // `engine` is the panel's QML engine, which turns replies into JavaScript objects.
    PluginPanelBridge(core::CommandBus& bus, const core::PluginManager& plugins, QString pluginId, QJSEngine& engine,
                      QObject* parent = nullptr);

    Q_INVOKABLE void execute(const QString& commandId, const QVariantMap& params, const QJSValue& callback);

    [[nodiscard]] QVariantMap settings() const { return m_settings; }
    [[nodiscard]] QString pluginId() const { return m_pluginId; }

Q_SIGNALS:
    void settingsChanged();

private:
    core::CommandBus& m_bus;
    QJSEngine& m_engine;
    QString m_pluginId;
    QVariantMap m_settings;
};

}  // namespace mikserstudio::ui
