#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>

namespace mikserstudio::api {

// The default location: QStandardPaths::AppLocalDataLocation + "/api.json"; empty if unknown.
[[nodiscard]] QString defaultDiscoveryFilePath();

// Atomically writes `contents` to `path` (creating the folder) with owner-only permissions.
[[nodiscard]] std::expected<void, QString> writeDiscoveryFile(const QString& path, const QJsonObject& contents);

// Removes the file only if it still holds `token`, so a second Mikser Studio instance that has since
// taken the file over keeps it.
void removeDiscoveryFile(const QString& path, const QString& token);

}  // namespace mikserstudio::api
