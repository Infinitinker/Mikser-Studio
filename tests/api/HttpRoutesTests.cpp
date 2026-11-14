// The local API's /v1 HTTP routes: health without a token, the bearer token for the rest, listing and
// running commands, the status codes of command errors, and the request size limit.
#include "ApiTestSupport.h"

#include "mikserstudio/api/ApiServer.h"

#include <QHostAddress>
#include <QTcpSocket>

using namespace Qt::StringLiterals;
using mikserstudio::tests::ApiFixture;
using mikserstudio::tests::sendRequest;

TEST_CASE("GET /v1/health answers without a token", "[api][http]")
{
    ApiFixture fixture;
    const auto reply = sendRequest("GET", fixture.url(u"/v1/health"_s));
    REQUIRE(reply.status == 200);
    CHECK(reply.headers.value(QHttpHeaders::WellKnownHeader::ContentType).startsWith("application/json"));
    CHECK(reply.headers.value(QHttpHeaders::WellKnownHeader::CacheControl) == "no-store");
    CHECK_FALSE(reply.headers.contains(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin));
    const QJsonObject body = reply.object();
    CHECK(body.value(u"name"_s).toString() == u"Mikser Studio"_s);
    CHECK_FALSE(body.value(u"version"_s).toString().isEmpty());
    CHECK(body.value(u"apiVersion"_s).toInt() == mikserstudio::api::kApiVersion);

    const auto post = sendRequest("POST", fixture.url(u"/v1/health"_s), "{}");
    CHECK(post.status == 405);
    CHECK(post.errorCode() == u"method_not_allowed"_s);
}

TEST_CASE("Routes other than health need the bearer token", "[api][http][auth]")
{
    ApiFixture fixture;
    const QUrl commands = fixture.url(u"/v1/commands"_s);

    SECTION("no Authorization header")
    {
        const auto reply = sendRequest("GET", commands);
        CHECK(reply.status == 401);
        CHECK(reply.errorCode() == u"unauthorized"_s);
        CHECK(reply.headers.value(QHttpHeaders::WellKnownHeader::WWWAuthenticate) == "Bearer");
    }
    SECTION("wrong token")
    {
        CHECK(sendRequest("GET", commands, {}, QByteArray("Bearer wrong-token-0123456789")).status == 401);
        QByteArray almost = fixture.bearer();
        almost.back() = almost.back() == 'A' ? 'B' : 'A';
        CHECK(sendRequest("GET", commands, {}, almost).status == 401);
        CHECK(sendRequest("GET", commands, {}, fixture.bearer() + "x").status == 401);
        CHECK(sendRequest("GET", commands, {}, QByteArray("Bearer")).status == 401);
    }
    SECTION("wrong scheme or token only in the query")
    {
        CHECK(sendRequest("GET", commands, {}, "Basic " + fixture.server->token().toUtf8()).status == 401);
        QUrl withQuery = commands;
        withQuery.setQuery(u"token="_s + fixture.server->token());
        CHECK(sendRequest("GET", withQuery).status == 401);
    }
    SECTION("unauthorized POST does not run the command")
    {
        CHECK(sendRequest("POST", fixture.url(u"/v1/commands/test.increment"_s), "{}").status == 401);
        CHECK(fixture.counter == 0);
    }
    SECTION("the scheme is case-insensitive")
    {
        CHECK(sendRequest("GET", commands, {}, "bearer " + fixture.server->token().toUtf8()).status == 200);
    }
}

TEST_CASE("GET /v1/commands lists every spec and GET /v1/commands/<id> returns one", "[api][http]")
{
    ApiFixture fixture;

    const auto list = fixture.get(u"/v1/commands"_s);
    REQUIRE(list.status == 200);
    QStringList ids;
    for (const QJsonValue spec : list.array())
        ids.append(spec.toObject().value(u"id"_s).toString());
    CHECK(ids == QStringList{u"edit.redo"_s, u"edit.undo"_s, u"test.busy"_s, u"test.echo"_s, u"test.fail"_s,
                             u"test.increment"_s, u"test.noop"_s, u"test.refuse"_s});

    const auto one = fixture.get(u"/v1/commands/test.increment"_s);
    REQUIRE(one.status == 200);
    CHECK(one.object() == mikserstudio::core::toJson(*fixture.bus.spec(u"test.increment"_s)));
    CHECK(one.object().value(u"undoable"_s).toBool());

    const auto missing = fixture.get(u"/v1/commands/test.missing"_s);
    CHECK(missing.status == 404);
    CHECK(missing.errorCode() == u"unknown_command"_s);
}

TEST_CASE("POST /v1/commands/<id> executes the command", "[api][http]")
{
    ApiFixture fixture;

    SECTION("result is wrapped in {result}")
    {
        const auto reply = fixture.post(u"/v1/commands/test.echo"_s, R"({"text": "héllo"})");
        REQUIRE(reply.status == 200);
        CHECK(reply.object() == QJsonObject{{u"result"_s, QJsonObject{{u"text"_s, u"héllo"_s}}}});
    }
    SECTION("defaults are applied and the edit lands on the undo stack")
    {
        const auto reply = fixture.post(u"/v1/commands/test.increment"_s, "{}");
        REQUIRE(reply.status == 200);
        CHECK(reply.object().value(u"result"_s).toObject().value(u"value"_s).toInt() == 1);
        CHECK(fixture.counter == 1);
        CHECK(fixture.undo.canUndo());

        const auto undo = fixture.post(u"/v1/commands/edit.undo"_s, {});
        REQUIRE(undo.status == 200);
        CHECK(undo.object().value(u"result"_s).toObject().value(u"text"_s).toString() == u"Increment Counter"_s);
        CHECK(fixture.counter == 0);
    }
    SECTION("an empty or whitespace body means {}")
    {
        CHECK(fixture.post(u"/v1/commands/test.noop"_s, {}).status == 200);
        CHECK(fixture.post(u"/v1/commands/test.noop"_s, " \r\n").status == 200);
        CHECK(fixture.post(u"/v1/commands/test.increment"_s, {}).status == 200);
        CHECK(fixture.counter == 1);
    }
}

TEST_CASE("Command errors map to HTTP status codes", "[api][http]")
{
    ApiFixture fixture;

    SECTION("invalid params: 400")
    {
        const auto reply = fixture.post(u"/v1/commands/test.echo"_s, R"({"text": 5})");
        CHECK(reply.status == 400);
        CHECK(reply.errorCode() == u"invalid_params"_s);
        CHECK(reply.object().value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"/text"_s));
        CHECK(fixture.post(u"/v1/commands/test.increment"_s, R"({"by": 0})").status == 400);
        CHECK(fixture.counter == 0);
    }
    SECTION("body that is not an object: 400")
    {
        const auto reply = fixture.post(u"/v1/commands/test.echo"_s, "[1, 2]");
        CHECK(reply.status == 400);
        CHECK(reply.errorCode() == u"invalid_params"_s);
    }
    SECTION("malformed JSON: 400")
    {
        const auto reply = fixture.post(u"/v1/commands/test.echo"_s, R"({"text": )");
        CHECK(reply.status == 400);
        CHECK(reply.errorCode() == u"malformed_json"_s);
    }
    SECTION("unknown command: 404")
    {
        const auto reply = fixture.post(u"/v1/commands/test.missing"_s, "{}");
        CHECK(reply.status == 404);
        CHECK(reply.errorCode() == u"unknown_command"_s);
    }
    SECTION("not available: 409")
    {
        const auto reply = fixture.post(u"/v1/commands/test.busy"_s, {});
        CHECK(reply.status == 409);
        CHECK(reply.errorCode() == u"not_available"_s);
        CHECK(fixture.post(u"/v1/commands/edit.undo"_s, {}).status == 409);
    }
    SECTION("needs confirmation and canceled: 409; permission denied: 403")
    {
        const auto ask = fixture.post(u"/v1/commands/test.refuse"_s, R"({"code": "needs_confirmation"})");
        CHECK(ask.status == 409);
        CHECK(ask.errorCode() == u"needs_confirmation"_s);
        CHECK(ask.object().value(u"error"_s).toObject().value(u"message"_s).toString() ==
              u"The test command refused with needs_confirmation."_s);
        const auto canceled = fixture.post(u"/v1/commands/test.refuse"_s, R"({"code": "canceled"})");
        CHECK(canceled.status == 409);
        CHECK(canceled.errorCode() == u"canceled"_s);
        const auto denied = fixture.post(u"/v1/commands/test.refuse"_s, R"({"code": "permission_denied"})");
        CHECK(denied.status == 403);
        CHECK(denied.errorCode() == u"permission_denied"_s);
    }
    SECTION("failed: 500 with the handler's message")
    {
        const auto reply = fixture.post(u"/v1/commands/test.fail"_s, {});
        CHECK(reply.status == 500);
        CHECK(reply.errorCode() == u"failed"_s);
        CHECK(reply.object().value(u"error"_s).toObject().value(u"message"_s).toString() ==
              u"The test command failed on purpose."_s);
    }
}

TEST_CASE("Unknown routes and wrong methods are rejected", "[api][http]")
{
    ApiFixture fixture;

    const auto unknown = fixture.get(u"/v1/nothing"_s);
    CHECK(unknown.status == 404);
    CHECK(unknown.errorCode() == u"not_found"_s);
    CHECK(fixture.get(u"/v1/commands/"_s).status == 404);
    CHECK(fixture.get(u"/v1/commands/test.echo/extra"_s).status == 404);
    CHECK(fixture.get(u"/"_s).status == 404);

    const auto put = sendRequest("PUT", fixture.url(u"/v1/commands/test.echo"_s), "{}", fixture.bearer());
    CHECK(put.status == 405);
    CHECK(put.headers.value(QHttpHeaders::WellKnownHeader::Allow) == "GET, POST");
    CHECK(fixture.post(u"/v1/commands"_s, "{}").status == 405);
    CHECK(sendRequest("OPTIONS", fixture.url(u"/v1/commands"_s), {}, fixture.bearer()).status == 405);

    const auto events = fixture.get(u"/v1/events"_s);
    CHECK(events.status == 426);
    CHECK(events.errorCode() == u"upgrade_required"_s);
}

TEST_CASE("Request bodies over 1 MiB are rejected", "[api][http]")
{
    ApiFixture fixture;

    QByteArray justFits(mikserstudio::api::kMaxRequestBodyBytes, ' ');
    justFits.front() = '{';
    justFits.back() = '}';
    CHECK(fixture.post(u"/v1/commands/test.increment"_s, justFits).status == 200);
    CHECK(fixture.counter == 1);

    SECTION("slightly too large: JSON 413")
    {
        QByteArray tooLarge = justFits;
        tooLarge.insert(1, ' ');
        const auto reply = fixture.post(u"/v1/commands/test.increment"_s, tooLarge);
        CHECK(reply.status == 413);
        CHECK(reply.errorCode() == u"payload_too_large"_s);
    }
    SECTION("far too large: refused before the body is read")
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, fixture.server->port());
        REQUIRE(socket.waitForConnected(5000));
        socket.write(
            "POST /v1/commands/test.increment HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: " + fixture.bearer() +
            "\r\nContent-Length: " + QByteArray::number(64 * mikserstudio::api::kMaxRequestBodyBytes) +
            "\r\nExpect: 100-continue\r\n\r\n");
        REQUIRE(mikserstudio::tests::waitFor([&] { return socket.canReadLine(); }));
        CHECK(socket.readLine().startsWith("HTTP/1.1 413"));
    }
    CHECK(fixture.counter == 1);
}
