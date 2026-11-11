#include "PluginSettingsStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

PluginSettingsStore::PluginSettingsStore(QString directory)
    : m_directory(std::move(directory))
{
}

std::expected<QJsonObject, QString> PluginSettingsStore::load(const QString& pluginId) const
{
    return read(pluginId + u".json"_s);
}

std::expected<void, QString> PluginSettingsStore::save(const QString& pluginId, const QJsonObject& settings) const
{
    return write(pluginId + u".json"_s, settings);
}

std::expected<QSet<QString>, QString> PluginSettingsStore::loadDisabled() const
{
    auto json = read(u"plugins.json"_s);
    if (!json)
        return std::unexpected(json.error());
    QSet<QString> disabled;
    for (const QJsonValue& id : json->value(u"disabled").toArray())
        disabled.insert(id.toString());
    return disabled;
}

std::expected<void, QString> PluginSettingsStore::saveDisabled(const QSet<QString>& disabled) const
{
    QStringList ids(disabled.begin(), disabled.end());
    std::ranges::sort(ids);
    return write(u"plugins.json"_s, QJsonObject{{u"disabled"_s, QJsonArray::fromStringList(ids)}});
}

std::expected<QJsonObject, QString> PluginSettingsStore::read(const QString& fileName) const
{
    QFile file(QDir(m_directory).filePath(fileName));
    if (!file.exists())
        return QJsonObject();
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(u"Cannot read %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::unexpected(u"%1 is not a valid JSON object (%2)"_s.arg(QDir::toNativeSeparators(file.fileName()),
                                                                          error.errorString()));
    }
    return document.object();
}

std::expected<void, QString> PluginSettingsStore::write(const QString& fileName, const QJsonObject& json) const
{
    if (!QDir().mkpath(m_directory))
        return std::unexpected(u"Cannot create the settings folder %1."_s.arg(QDir::toNativeSeparators(m_directory)));
    QSaveFile file(QDir(m_directory).filePath(fileName));
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(u"Cannot write %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return std::unexpected(u"Cannot save %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    return {};
}

}  // namespace mikserstudio::core
