#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>
#include <functional>
#include <span>

namespace mikserstudio::core {

// Upgrades a project document from `fromVersion` to `fromVersion + 1`.
struct ProjectMigration {
    int fromVersion = 0;
    std::function<std::expected<QJsonObject, QString>(QJsonObject)> apply;
};

// The migrations of the project format, one per format change. Empty while version 1 is the only
// format; add an entry here (and bump ProjectModel::kFormatVersion) whenever the format changes.
[[nodiscard]] std::span<const ProjectMigration> projectMigrations();

// Reads "version", rejects files from newer versions and applies migrations step by step until
// the document has `targetVersion`.
[[nodiscard]] std::expected<QJsonObject, QString> migrateProject(QJsonObject json, int targetVersion,
                                                                 std::span<const ProjectMigration> migrations);

}  // namespace mikserstudio::core
