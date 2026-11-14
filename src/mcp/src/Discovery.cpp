// Finding the discovery file of an instance and reading it.
#include "Discovery.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace mikserstudio::mcp {

namespace {

// The name each instance's folders take, as the app names itself.
QString folderName(const QString& instance)
{
    if (instance == u"dev"_s)
        return u"Mikser Studio Dev"_s;
    if (instance == u"e2e"_s)
        return u"Mikser Studio E2E"_s;
    return u"Mikser Studio"_s;
}

}  // namespace

QStringList instanceNames()
{
    return {u"installed"_s, u"dev"_s, u"e2e"_s};
}

QString defaultInstance()
{
    const QString named = qEnvironmentVariable("MIKSERSTUDIO_INSTANCE");
    return instanceNames().contains(named) ? named : u"installed"_s;
}

QString discoveryFile(const QString& instance)
{
    QString folder = qEnvironmentVariable("MIKSERSTUDIO_DATA_DIR");
    // The app's AppLocalDataLocation: it sets no organization, so its data folder is the generic one
    // with the app's name.
    if (folder.isEmpty())
        folder =
            QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(folderName(instance));
    return QDir(folder).filePath(u"api.json"_s);
}

std::optional<Discovery> readDiscovery(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonValue baseUrl = data.value(u"baseUrl"_s);
    const QJsonValue eventsUrl = data.value(u"eventsUrl"_s);
    const QJsonValue token = data.value(u"token"_s);
    if (data.value(u"apiVersion"_s) != QJsonValue(1) || !baseUrl.isString() || !eventsUrl.isString() ||
        !token.isString()) {
        return std::nullopt;
    }
    return Discovery{.baseUrl = QUrl(baseUrl.toString()),
                     .eventsUrl = QUrl(eventsUrl.toString()),
                     .token = token.toString().toUtf8()};
}

}  // namespace mikserstudio::mcp
