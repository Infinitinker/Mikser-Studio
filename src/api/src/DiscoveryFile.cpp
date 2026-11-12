// Writing and removing the discovery file (api.json) of the local API.
#include "DiscoveryFile.h"

#include "Logging.h"
#include "OwnerOnlyPermissions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

QString defaultDiscoveryFilePath()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return folder.isEmpty() ? QString() : folder + u"/api.json"_s;
}

std::expected<void, QString> writeDiscoveryFile(const QString& path, const QJsonObject& contents)
{
    const auto failure = [&path](const QString& reason) {
        return std::unexpected(
            u"Could not write the API discovery file %1: %2. Check that the folder is writable."_s.arg(
                QDir::toNativeSeparators(path), reason));
    };

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return failure(u"the folder cannot be created"_s);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return failure(file.errorString());
    // Restrict the temporary file before the token is written to it (honoured on Unix).
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = QJsonDocument(contents).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size())
        return failure(file.errorString());
    if (!file.commit())
        return failure(file.errorString());

    if (auto restricted = restrictToOwner(path); !restricted) {
        QFile::remove(path);
        return failure(u"its permissions cannot be restricted to the current user ("_s + restricted.error() + u')');
    }
    return {};
}

void removeDiscoveryFile(const QString& path, const QString& token)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (document.object().value(u"token"_s).toString() != token) {
        qCDebug(lcApi) << "Keeping the discovery file; another instance has replaced it";
        return;
    }
    if (!QFile::remove(path))
        qCWarning(lcApi) << "Could not remove the API discovery file" << QDir::toNativeSeparators(path);
}

}  // namespace mikserstudio::api
