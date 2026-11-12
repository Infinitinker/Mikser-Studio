// Project migrations: the built-in ones cover every older format, run step by step to the target version, and
// refuse missing, invalid and newer versions.
#include "TestSupport.h"

#include "ProjectMigrations.h"
#include "mikserstudio/core/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

TEST_CASE("The built-in migrations cover every older format", "[project][migration]")
{
    for (int version = 1; version < ProjectModel::kFormatVersion; ++version) {
        const auto migrations = projectMigrations();
        CHECK(std::ranges::find(migrations, version, &ProjectMigration::fromVersion) != migrations.end());
    }
    ProjectModel project;
    const auto current = migrateProject(project.toJson(), ProjectModel::kFormatVersion, projectMigrations());
    REQUIRE(current);
    CHECK(*current == project.toJson());
}

TEST_CASE("Migrations run step by step up to the target version", "[project][migration]")
{
    const std::array<ProjectMigration, 2> migrations{
        ProjectMigration{1,
                         [](QJsonObject json) -> std::expected<QJsonObject, QString> {
                             json.insert(u"fps"_s, json.take(u"rate"_s));
                             return json;
                         }},
        ProjectMigration{2,
                         [](QJsonObject json) -> std::expected<QJsonObject, QString> {
                             json.insert(u"steps"_s, json.value(u"steps").toInt() + 1);
                             return json;
                         }},
    };

    const auto migrated = migrateProject(json(R"({"version": 1, "rate": 25})"), 3, migrations);
    REQUIRE(migrated);
    CHECK(*migrated == json(R"({"version": 3, "fps": 25, "steps": 1})"));

    const auto partial = migrateProject(json(R"({"version": 2, "steps": 5})"), 3, migrations);
    REQUIRE(partial);
    CHECK(partial->value(u"steps").toInt() == 6);

    const auto missing = migrateProject(json(R"({"version": 1})"), 4, migrations);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().contains(u"format 3"_s));
}

TEST_CASE("Migration rejects missing, invalid and newer versions", "[project][migration]")
{
    CHECK_FALSE(migrateProject(json(R"({})"), 1, {}));
    CHECK_FALSE(migrateProject(json(R"({"version": "1"})"), 1, {}));
    CHECK_FALSE(migrateProject(json(R"({"version": 0})"), 1, {}));
    CHECK_FALSE(migrateProject(json(R"({"version": 1.5})"), 2, {}));
    const auto newer = migrateProject(json(R"({"version": 2})"), 1, {});
    REQUIRE_FALSE(newer);
    CHECK(newer.error().contains(u"newer"_s));
    // A huge version is reported as written, not cast to a garbage integer.
    const auto huge = migrateProject(json(R"({"version": 1e300})"), 1, {});
    REQUIRE_FALSE(huge);
    CHECK(huge.error().contains(u"format 1e+300"_s));

    const std::array<ProjectMigration, 1> failing{ProjectMigration{
        1, [](const QJsonObject&) -> std::expected<QJsonObject, QString> { return std::unexpected(u"bad data"_s); }}};
    const auto failed = migrateProject(json(R"({"version": 1})"), 2, failing);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().contains(u"bad data"_s));
}
