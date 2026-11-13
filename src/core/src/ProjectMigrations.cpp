// Checking a project file's version and applying the migrations that bring it to the current format.
#include "ProjectMigrations.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

std::span<const ProjectMigration> projectMigrations()
{
    return {};
}

std::expected<QJsonObject, QString> migrateProject(QJsonObject json, int targetVersion,
                                                   std::span<const ProjectMigration> migrations)
{
    const QJsonValue versionValue = json.value(u"version");
    const double number = versionValue.toDouble(-1);
    if (!versionValue.isDouble() || std::trunc(number) != number || number < 1)
        return std::unexpected(
            u"The project file has no valid “version” field: it is not a Mikser Studio project, or it is damaged. "
            u"Open a .msproj file that Mikser Studio saved."_s);
    if (number > targetVersion) {
        return std::unexpected(
            u"The project was saved by a newer Mikser Studio (format %1, this version reads up to %2). Update Mikser Studio to open it."_s
                .arg(number, 0, 'g', 15)
                .arg(targetVersion));
    }

    for (int version = static_cast<int>(number); version < targetVersion; ++version) {
        const auto step = std::ranges::find(migrations, version, &ProjectMigration::fromVersion);
        if (step == migrations.end())
            return std::unexpected(
                u"Project format %1 cannot be upgraded: no migration to format %2 exists."_s.arg(version).arg(version +
                                                                                                              1));
        auto migrated = step->apply(std::move(json));
        if (!migrated)
            return std::unexpected(
                u"Upgrading the project from format %1 failed: %2"_s.arg(version).arg(migrated.error()));
        json = std::move(*migrated);
        json.insert(u"version"_s, version + 1);
    }
    return json;
}

}  // namespace mikserstudio::core
