// The instances MIKSERSTUDIO_INSTANCE chooses between, their names and app ids, and their per-user
// folders.
#include "mikserstudio/core/Instance.h"
#include "mikserstudio/core/UserFolders.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace Qt::StringLiterals;
using mikserstudio::core::instanceNamed;
using mikserstudio::core::kAppId;
using mikserstudio::core::UnsetInstance;

TEST_CASE("Unset, MIKSERSTUDIO_INSTANCE is the installed app in a release build and Dev in any other", "[instance]")
{
    const auto installed = instanceNamed({}, UnsetInstance::Installed);
    REQUIRE(installed.has_value());
    CHECK(installed->name == u"Mikser Studio"_s);
    CHECK(installed->appId == QString::fromLatin1(kAppId));

    const auto fromSource = instanceNamed({}, UnsetInstance::Dev);
    REQUIRE(fromSource.has_value());
    CHECK(fromSource->name == u"Mikser Studio Dev"_s);

    // These tests are a build from source, which never runs as the installed app.
    CHECK(mikserstudio::core::unsetInstance() == UnsetInstance::Dev);
}

TEST_CASE("The app run from source and the app under the E2E tests have names and ids of their own", "[instance]")
{
    for (const auto unset : {UnsetInstance::Installed, UnsetInstance::Dev}) {
        const auto dev = instanceNamed(u"dev"_s, unset);
        REQUIRE(dev.has_value());
        CHECK(dev->name == u"Mikser Studio Dev"_s);
        CHECK(dev->appId == u"io.github.infinitinker.mikserstudio.dev"_s);

        const auto e2e = instanceNamed(u"e2e"_s, unset);
        REQUIRE(e2e.has_value());
        CHECK(e2e->name == u"Mikser Studio E2E"_s);
        CHECK(e2e->appId == u"io.github.infinitinker.mikserstudio.e2e"_s);
    }
}

TEST_CASE("An unknown instance is an error that names the value and the ones to use", "[instance]")
{
    const auto instance = instanceNamed(u"Dev"_s, UnsetInstance::Dev);
    REQUIRE_FALSE(instance.has_value());
    CHECK(instance.error().contains(u"“Dev”"_s));
    CHECK(instance.error().contains(u"dev or e2e"_s));
}

TEST_CASE("The per-user folders are the instance's own, with the logs and the discovery file in its data", "[instance]")
{
    const QString previousName = QCoreApplication::applicationName();
    const auto dev = instanceNamed(u"dev"_s, UnsetInstance::Dev);
    REQUIRE(dev.has_value());
    mikserstudio::core::applyInstance(*dev);

    const auto folders = mikserstudio::core::UserFolders::resolve();
    for (const QString& folder : {folders.config, folders.data, folders.cache})
        CHECK(folder.contains(u"Mikser Studio Dev"_s));
    CHECK(folders.logs == QDir(folders.data).filePath(u"logs"_s));
    CHECK(folders.discoveryFile == QDir(folders.data).filePath(u"api.json"_s));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    qputenv("MIKSERSTUDIO_DATA_DIR", dir.path().toUtf8());
    const auto overridden = mikserstudio::core::UserFolders::resolve();
    qunsetenv("MIKSERSTUDIO_DATA_DIR");
    CHECK(overridden.discoveryFile == QDir(dir.path()).filePath(u"api.json"_s));
    CHECK(overridden.config == folders.config);

    QCoreApplication::setApplicationName(previousName);
}
