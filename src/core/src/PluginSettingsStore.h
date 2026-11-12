// Reading and writing the plugins' settings files and the list of disabled plugins.
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>

#include <expected>

namespace mikserstudio::core {

// Reads and writes plugin settings files: <directory>/<plugin-id>.json for each plugin and
// <directory>/plugins.json for the list of disabled plugins (plugin ids always contain a dot,
// so the names never clash).
class PluginSettingsStore {
public:
    explicit PluginSettingsStore(QString directory);

    // An empty object when the plugin has no settings file yet.
    [[nodiscard]] std::expected<QJsonObject, QString> load(const QString& pluginId) const;
    [[nodiscard]] std::expected<void, QString> save(const QString& pluginId, const QJsonObject& settings) const;

    [[nodiscard]] std::expected<QSet<QString>, QString> loadDisabled() const;
    [[nodiscard]] std::expected<void, QString> saveDisabled(const QSet<QString>& disabled) const;

private:
    [[nodiscard]] std::expected<QJsonObject, QString> read(const QString& fileName) const;
    [[nodiscard]] std::expected<void, QString> write(const QString& fileName, const QJsonObject& json) const;

    QString m_directory;
};

}  // namespace mikserstudio::core
