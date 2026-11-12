// The instances MIKSERSTUDIO_INSTANCE chooses between, and their names and app ids.
#include "AppId.h"
#include "Instance.h"

#include <catch2/catch_test_macros.hpp>

using namespace Qt::StringLiterals;
using mikserstudio::app::instanceNamed;
using mikserstudio::app::kAppId;

TEST_CASE("Without MIKSERSTUDIO_INSTANCE, the installed app's name and id", "[instance]")
{
    const auto instance = instanceNamed({});
    REQUIRE(instance.has_value());
    CHECK(instance->name == u"Mikser Studio"_s);
    CHECK(instance->appId == QString::fromLatin1(kAppId));
}

TEST_CASE("The app run from source and the app under the E2E tests have names and ids of their own", "[instance]")
{
    const auto dev = instanceNamed(u"dev"_s);
    REQUIRE(dev.has_value());
    CHECK(dev->name == u"Mikser Studio Dev"_s);
    CHECK(dev->appId == u"io.github.infinitinker.mikserstudio.dev"_s);

    const auto e2e = instanceNamed(u"e2e"_s);
    REQUIRE(e2e.has_value());
    CHECK(e2e->name == u"Mikser Studio E2E"_s);
    CHECK(e2e->appId == u"io.github.infinitinker.mikserstudio.e2e"_s);
}

TEST_CASE("An unknown instance is an error that names the value and the ones to use", "[instance]")
{
    const auto instance = instanceNamed(u"Dev"_s);
    REQUIRE_FALSE(instance.has_value());
    CHECK(instance.error().contains(u"“Dev”"_s));
    CHECK(instance.error().contains(u"dev or e2e"_s));
}
