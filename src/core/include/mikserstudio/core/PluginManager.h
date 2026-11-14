// Plugin discovery and lifecycle: reading manifests, starting and stopping plugins through a plugin
// runtime, and storing their settings.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <expected>
#include <memory>

namespace mikserstudio::core {

class CommandBus;
class PluginRuntime;

// A UI panel a plugin contributes. QML is loaded by the UI and bound to the plugin's commands.
struct PanelContribution {
    QString id;  // Unique inside the plugin; registered as "<plugin-id>/<panel-id>".
    QString title;
    QString qml;  // Path relative to the plugin directory.
    QString defaultArea;  // "left" | "right" | "bottom" | "center" | "floating".
};

// Parsed mikserstudio-plugin.json, version 1. Reading a manifest never runs plugin code.
//
// {
//   "id": "mikserstudio.examples.hello",   // reverse-dns-ish, lower case, dots
//   "name": "Hello", "version": "0.1.0",   // SemVer
//   "description": "...",
//   "apiVersion": 1,                       // Must equal kPluginApiVersion
//   "entry": "main.lua",                   // The Lua file that starts the plugin
//   "permissions": ["project.read", "ui"], // What the plugin may do (docs/architecture.md)
//   "network": ["127.0.0.1:11434"],        // Where its jobs may connect (NetworkEntry), with "network"
//   "settingsSchema": { JSON Schema },
//   "panels": [ { "id", "title", "qml", "defaultArea" } ]
// }
//
// There is no "kind": every plugin is a Lua plugin. A manifest that names one ("native", "python")
// is refused with the reason.
struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    int apiVersion = 0;
    QString entry;
    QStringList permissions;
    QStringList network;  // Lower case.
    QJsonObject settingsSchema;
    // The section of the Settings window that shows a built-in plugin's settings, such as "AI tools".
    QString settingsSection;
    QList<PanelContribution> panels;
    QString directory;  // Absolute directory that contained the manifest.
};

inline constexpr int kPluginApiVersion = 1;
inline constexpr auto kPluginManifestFileName = "mikserstudio-plugin.json";

// Validates the manifest (required fields, id/version format, apiVersion, entry, permissions,
// network, settingsSchema, settingsSection, panels) and fills defaults: description "", permissions [], network [],
// panel defaultArea "right".
[[nodiscard]] std::expected<PluginManifest, QString> parsePluginManifest(const QJsonObject& json,
                                                                         const QString& directory);

// Where a plugin was found.
enum class PluginSource {
    BuiltIn,  // Shipped with the app: on, with the permissions its manifest asks for.
    User,  // The person's own: off until they enable it, which grants its permissions but plugins.manage.
    Development,  // A folder MIKSERSTUDIO_PLUGIN_PATH names: on, as the person's own otherwise,
                  // but with its panels.
};

enum class PluginStatus { Discovered, Starting, Active, Failed, Disabled };

struct PluginState {
    PluginManifest manifest;
    PluginStatus status = PluginStatus::Discovered;
    QString error;  // Set when status is Failed, in user terms.
    PluginSource source = PluginSource::BuiltIn;
    QStringList granted;  // The permissions it runs with (or would).
};

// Discovers plugins in search paths, starts them through a PluginRuntime (the Lua host) and keeps
// their settings (validated against settingsSchema, stored as JSON in the settings directory). A
// failing plugin never takes down the app.
class PluginManager final : public QObject {
    Q_OBJECT

public:
    PluginManager(CommandBus& bus, const QString& settingsDirectory, QObject* parent = nullptr);
    ~PluginManager() override;  // Stops every plugin.

    // What runs the plugins; it must outlive their activation. Without one, activating fails.
    void setRuntime(PluginRuntime* runtime);

    // Each subdirectory with a manifest is a plugin. A plugin id found in an earlier path wins.
    void addSearchPath(const QString& directory, PluginSource source = PluginSource::BuiltIn);
    void discover();  // (Re)reads manifests; keeps running plugins running.

    [[nodiscard]] QList<PluginState> plugins() const;
    [[nodiscard]] const PluginState* plugin(const QString& id) const;

    // Starts a plugin. It is Starting until the runtime says it has started (Active) or failed
    // (Failed, and pluginFailed() is emitted). Fails at once, with the reason, for a plugin that
    // can't start at all.
    std::expected<void, QString> activate(const QString& id);
    // Stops the plugin: its commands are unregistered and its pending runs canceled.
    void deactivate(const QString& id);
    void activateAll();  // Starts every discovered plugin that is not disabled.
    // True once activateAll() has run and no plugin is starting any more.
    [[nodiscard]] bool ready() const;

    // Disabled plugins are never activated; the choice is stored in <settingsDirectory>/plugins.json.
    // Disabling an active plugin deactivates it. Enabling one of the person's own plugins grants it
    // the permissions its manifest asks for.
    std::expected<void, QString> setEnabled(const QString& id, bool enabled);

    [[nodiscard]] QJsonObject settings(const QString& id) const;  // With schema defaults applied.
    // Replaces the stored settings after validating them (with defaults applied) against the
    // manifest's settingsSchema; saved as <settingsDirectory>/<plugin-id>.json. A running plugin
    // gets them at once.
    std::expected<void, QString> setSettings(const QString& id, const QJsonObject& settings);

Q_SIGNALS:
    void pluginsChanged();
    void pluginFailed(const QString& id, const QString& message);
    void settingsChanged(const QString& id, const QJsonObject& settings);  // With defaults applied.

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace mikserstudio::core
