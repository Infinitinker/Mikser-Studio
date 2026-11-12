// The discovery file (api.json): how it is written and removed, so other programs can find the local API's
// address and session token. Where it lives is the app's to say (core::UserFolders).
#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>

namespace mikserstudio::api {

// Atomically writes `contents` to `path` (creating the folder) with owner-only permissions.
[[nodiscard]] std::expected<void, QString> writeDiscoveryFile(const QString& path, const QJsonObject& contents);

// Removes the file only if it still holds `token`, so a second Mikser Studio instance that has since
// taken the file over keeps it.
void removeDiscoveryFile(const QString& path, const QString& token);

}  // namespace mikserstudio::api
