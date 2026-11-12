// Session tokens: generated ones are long, URL-safe and unique, a supplied one is validated, and tokens are
// compared in constant time.
#include "ApiTestSupport.h"

#include "Auth.h"

#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

TEST_CASE("Generated tokens are long, URL-safe and unique", "[api][auth]")
{
    const QRegularExpression urlSafe(u"^[A-Za-z0-9_-]{43}$"_s);
    QSet<QString> seen;
    for (int i = 0; i < 100; ++i) {
        const QString token = mikserstudio::api::generateToken();
        CHECK(urlSafe.match(token).hasMatch());
        CHECK(mikserstudio::api::checkToken(token).has_value());
        seen.insert(token);
    }
    CHECK(seen.size() == 100);
}

TEST_CASE("checkToken accepts only header-safe tokens of a sane length", "[api][auth]")
{
    CHECK(mikserstudio::api::checkToken(u"0123456789abcdef"_s).has_value());
    CHECK_FALSE(mikserstudio::api::checkToken(u"short"_s).has_value());
    CHECK_FALSE(mikserstudio::api::checkToken(QString(513, u'a')).has_value());
    CHECK_FALSE(mikserstudio::api::checkToken(u"0123456789 abcdef"_s).has_value());
    CHECK_FALSE(mikserstudio::api::checkToken(u"0123456789abcdefé"_s).has_value());
}

TEST_CASE("constantTimeEquals compares whole tokens", "[api][auth]")
{
    using mikserstudio::api::constantTimeEquals;
    CHECK(constantTimeEquals("secret-token", "secret-token"));
    CHECK_FALSE(constantTimeEquals("secret-tokeN", "secret-token"));
    CHECK_FALSE(constantTimeEquals("secret-toke", "secret-token"));
    CHECK_FALSE(constantTimeEquals("secret-token-", "secret-token"));
    CHECK_FALSE(constantTimeEquals("", "secret-token"));
    CHECK_FALSE(constantTimeEquals(QByteArrayView("secret-token\0", 13), "secret-token"));
}

TEST_CASE("A caller-supplied token is used and validated", "[api][auth]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);

    SECTION("valid token")
    {
        mikserstudio::api::ApiServer server(
            bus, {.port = 0, .token = u"my-fixed-token-0123456789"_s, .discoveryFile = dir.filePath(u"api.json"_s)});
        REQUIRE(server.start().has_value());
        CHECK(server.token() == u"my-fixed-token-0123456789"_s);
        const auto reply = mikserstudio::tests::sendRequest("GET", QUrl(server.baseUrl() + u"/v1/commands"_s), {},
                                                            QByteArray("Bearer my-fixed-token-0123456789"));
        CHECK(reply.status == 200);
    }
    SECTION("weak token")
    {
        mikserstudio::api::ApiServer server(
            bus, {.port = 0, .token = u"1234"_s, .discoveryFile = dir.filePath(u"api.json"_s)});
        const auto started = server.start();
        REQUIRE_FALSE(started.has_value());
        CHECK(started.error().contains(u"token"_s));
        CHECK_FALSE(server.isRunning());
    }
}
