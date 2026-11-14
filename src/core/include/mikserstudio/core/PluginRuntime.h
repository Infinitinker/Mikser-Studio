// The seam between the plugin manager and the code that runs plugins: the manager discovers
// plugins, keeps their settings and decides which run; a runtime, such as the Lua host, runs them.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <memory>

namespace mikserstudio::core {

struct PluginManifest;

// A running plugin. Destroying it stops the plugin: its commands are unregistered, its pending
// runs finish as canceled and its jobs are canceled.
class PluginInstance {
public:
    PluginInstance() = default;
    virtual ~PluginInstance() = default;
    PluginInstance(const PluginInstance&) = delete;
    PluginInstance& operator=(const PluginInstance&) = delete;

    // The plugin's settings changed (with the schema's defaults applied).
    virtual void setSettings(const QJsonObject& settings) = 0;
};

struct PluginActivation {
    // The permissions the plugin was granted: its manifest's, as the person agreed to them.
    QStringList granted;
    QJsonObject settings;  // With the schema's defaults applied.
    // Called on the main thread each time the plugin starts: when its entry file has run, or when
    // it could not start, with the reason in user terms. A plugin starts on activation and again on
    // each reload, so one that has started may fail later, and one that failed may start.
    std::function<void()> started;
    std::function<void(const QString& message)> failed;
};

// Runs plugins of one kind. Activating a plugin starts it in the background: the instance comes
// back at once, and the activation's callbacks say how starting went.
class PluginRuntime {
public:
    PluginRuntime() = default;
    virtual ~PluginRuntime() = default;
    PluginRuntime(const PluginRuntime&) = delete;
    PluginRuntime& operator=(const PluginRuntime&) = delete;

    // Fails at once, with the reason in user terms, when the plugin can't even begin to start
    // (such as a missing entry file).
    [[nodiscard]] virtual std::expected<std::unique_ptr<PluginInstance>, QString> activate(
        const PluginManifest& manifest, PluginActivation activation) = 0;
};

}  // namespace mikserstudio::core
