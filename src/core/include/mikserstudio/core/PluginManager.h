// Plugin discovery and lifecycle: reading manifests, activating and deactivating plugins, and storing their
// settings.
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

// A UI panel a plugin contributes. QML is loaded by the UI and bound to the plugin's commands.
struct PanelContribution {
    QString id;  // Unique inside the plugin; registered as "<plugin-id>/<panel-id>".
    QString title;
    QString qml;  // Path relative to the plugin directory.
    QString defaultArea;  // "left" | "right" | "bottom" | "center" | "floating".
};

// Parsed mikserstudio-plugin.json. Reading a manifest never loads plugin code.
//
// {
//   "id": "mikserstudio.examples.hello",          // reverse-dns-ish, lower case, dots
//   "name": "Hello", "version": "0.1.0",   // SemVer
//   "description": "...",
//   "apiVersion": 1,                       // Must equal kPluginApiVersion
//   "kind": "native",                      // The one kind; "python" is refused with a message
//   "entry": "hello",                      // Library base name
//   "capabilities": ["commands", "panels"],
//   "settingsSchema": { JSON Schema },
//   "panels": [ { "id", "title", "qml", "defaultArea" } ]
// }
struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    int apiVersion = 0;
    QString kind;
    QString entry;
    QStringList capabilities;
    QJsonObject settingsSchema;
    QList<PanelContribution> panels;
    QString directory;  // Absolute directory that contained the manifest.
};

inline constexpr int kPluginApiVersion = 1;
inline constexpr auto kPluginManifestFileName = "mikserstudio-plugin.json";

// Validates the manifest (required fields, id/version format, apiVersion, entry, settingsSchema,
// panels) and fills defaults: description "", capabilities [], panel defaultArea "right".
[[nodiscard]] std::expected<PluginManifest, QString> parsePluginManifest(const QJsonObject& json,
                                                                         const QString& directory);

enum class PluginStatus { Discovered, Active, Failed, Disabled };

struct PluginState {
    PluginManifest manifest;
    PluginStatus status = PluginStatus::Discovered;
    QString error;  // Set when status is Failed, in user terms.
};

// Discovers plugins in search paths, activates native ones through the C ABI in
// sdk/include/mikserstudio/plugin_api.h and keeps their settings (validated against settingsSchema,
// stored as JSON in the settings directory). A failing plugin never takes down the app.
class PluginManager final : public QObject {
    Q_OBJECT

public:
    PluginManager(CommandBus& bus, const QString& settingsDirectory, QObject* parent = nullptr);
    ~PluginManager() override;  // Deactivates every active plugin.

    void addSearchPath(const QString& directory);  // Each subdirectory with a manifest is a plugin.
    void discover();  // (Re)reads manifests; keeps active plugins active.

    [[nodiscard]] QList<PluginState> plugins() const;
    [[nodiscard]] const PluginState* plugin(const QString& id) const;

    // Loads a native plugin and calls its activate(). On failure the library is unloaded, the
    // plugin is marked Failed, pluginFailed() is emitted and the reason returned.
    std::expected<void, QString> activate(const QString& id);
    // Unregisters the plugin's commands, calls its deactivate() and unloads it.
    void deactivate(const QString& id);
    void activateAll();  // Activates every discovered plugin that is not disabled.

    // Disabled plugins are never activated; the choice is stored in <settingsDirectory>/plugins.json.
    // Disabling an active plugin deactivates it.
    std::expected<void, QString> setEnabled(const QString& id, bool enabled);

    [[nodiscard]] QJsonObject settings(const QString& id) const;  // With schema defaults applied.
    // Replaces the stored settings after validating them (with defaults applied) against the
    // manifest's settingsSchema; saved as <settingsDirectory>/<plugin-id>.json.
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
