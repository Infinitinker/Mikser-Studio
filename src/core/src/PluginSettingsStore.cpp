// Reading and writing the plugins' settings files and the person's choice of plugins.
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

namespace {

QSet<QString> idSet(const QJsonValue& ids)
{
    QSet<QString> set;
    for (const QJsonValue id : ids.toArray())
        set.insert(id.toString());
    return set;
}

QJsonArray sortedIds(const QSet<QString>& set)
{
    QStringList ids(set.begin(), set.end());
    std::ranges::sort(ids);
    return QJsonArray::fromStringList(ids);
}

}  // namespace

std::expected<PluginChoices, QString> PluginSettingsStore::loadChoices() const
{
    auto json = read(u"plugins.json"_s);
    if (!json)
        return std::unexpected(json.error());
    return PluginChoices{.disabled = idSet(json->value(u"disabled")), .enabled = idSet(json->value(u"enabled"))};
}

std::expected<void, QString> PluginSettingsStore::saveChoices(const PluginChoices& choices) const
{
    return write(u"plugins.json"_s,
                 QJsonObject{{u"disabled"_s, sortedIds(choices.disabled)}, {u"enabled"_s, sortedIds(choices.enabled)}});
}

std::expected<QJsonObject, QString> PluginSettingsStore::read(const QString& fileName) const
{
    QFile file(QDir(m_directory).filePath(fileName));
    if (!file.exists())
        return QJsonObject();
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(
            u"Cannot read %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
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
        return std::unexpected(
            u"Cannot write %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return std::unexpected(
            u"Cannot save %1: %2"_s.arg(QDir::toNativeSeparators(file.fileName()), file.errorString()));
    return {};
}

}  // namespace mikserstudio::core
