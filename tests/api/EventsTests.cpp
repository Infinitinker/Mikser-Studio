#include "ApiTestSupport.h"

#include <QNetworkRequest>
#include <QWebSocket>

using namespace Qt::StringLiterals;
using mikserstudio::tests::ApiFixture;
using mikserstudio::tests::waitFor;

namespace {

// A /v1/events client that collects the JSON events it receives.
class EventClient {
public:
    EventClient()
    {
        QObject::connect(&socket, &QWebSocket::textMessageReceived, [this](const QString& message) {
            events.append(QJsonDocument::fromJson(message.toUtf8()).object());
        });
        QObject::connect(&socket, &QWebSocket::errorOccurred, [this](QAbstractSocket::SocketError) { failed = true; });
    }

    bool open(const QNetworkRequest& request)
    {
        socket.open(request);
        return waitFor([this] { return socket.state() == QAbstractSocket::ConnectedState || failed; }) &&
               socket.state() == QAbstractSocket::ConnectedState;
    }

    [[nodiscard]] QJsonObject find(const QString& type) const
    {
        for (const QJsonObject& event : events) {
            if (event.value(u"type"_s).toString() == type)
                return event;
        }
        return {};
    }

    bool waitForEvent(const QString& type)
    {
        return waitFor([this, &type] { return !find(type).isEmpty(); });
    }

    QWebSocket socket;
    QList<QJsonObject> events;
    bool failed = false;
};

QNetworkRequest queryRequest(const ApiFixture& fixture, const QString& token)
{
    QUrl url(fixture.server->eventsUrl());
    url.setQuery(u"token="_s + token);
    return QNetworkRequest(url);
}

}  // namespace

TEST_CASE("The events socket pushes command.executed and undo.changed", "[api][events]")
{
    ApiFixture fixture;
    EventClient client;
    REQUIRE(client.open(queryRequest(fixture, fixture.server->token())));
    REQUIRE(waitFor([&] { return fixture.server->eventClientCount() == 1; }));

    REQUIRE(fixture.post(u"/v1/commands/test.increment"_s, R"({"by": 2})").status == 200);
    REQUIRE(client.waitForEvent(u"command.executed"_s));
    REQUIRE(client.waitForEvent(u"undo.changed"_s));

    CHECK(client.find(u"command.executed"_s) ==
          QJsonObject{{u"type"_s, u"command.executed"_s},
                      {u"id"_s, u"test.increment"_s},
                      {u"params"_s, QJsonObject{{u"by"_s, 2}}},
                      {u"result"_s, QJsonObject{{u"value"_s, 2}}}});
    CHECK(client.find(u"undo.changed"_s) == QJsonObject{{u"type"_s, u"undo.changed"_s},
                                                        {u"canUndo"_s, true},
                                                        {u"canRedo"_s, false},
                                                        {u"undoText"_s, u"Increment Counter"_s},
                                                        {u"redoText"_s, QString()}});

    client.events.clear();
    REQUIRE(fixture.post(u"/v1/commands/edit.undo"_s, {}).status == 200);
    REQUIRE(waitFor([&] {
        const QJsonObject undo = client.find(u"undo.changed"_s);
        return !undo.isEmpty() && !client.find(u"command.executed"_s).isEmpty();
    }));
    const QJsonObject undo = client.find(u"undo.changed"_s);
    CHECK_FALSE(undo.value(u"canUndo"_s).toBool());
    CHECK(undo.value(u"canRedo"_s).toBool());
    CHECK(undo.value(u"redoText"_s).toString() == u"Increment Counter"_s);
    CHECK(client.find(u"command.executed"_s).value(u"id"_s).toString() == u"edit.undo"_s);
}

TEST_CASE("The events socket pushes failures and registry changes, also for commands run in-process",
          "[api][events]")
{
    ApiFixture fixture;
    EventClient client;
    QNetworkRequest request{QUrl(fixture.server->eventsUrl())};
    request.setRawHeader("Authorization", fixture.bearer());
    REQUIRE(client.open(request));
    REQUIRE(waitFor([&] { return fixture.server->eventClientCount() == 1; }));

    REQUIRE_FALSE(fixture.bus.execute(u"test.fail"_s).has_value());
    REQUIRE(client.waitForEvent(u"command.failed"_s));
    CHECK(client.find(u"command.failed"_s) ==
          QJsonObject{{u"type"_s, u"command.failed"_s},
                      {u"id"_s, u"test.fail"_s},
                      {u"params"_s, QJsonObject{}},
                      {u"error"_s, QJsonObject{{u"code"_s, u"failed"_s},
                                               {u"message"_s, u"The test command failed on purpose."_s}}}});

    mikserstudio::core::CommandSpec late;
    late.id = u"test.late"_s;
    late.title = u"Late"_s;
    REQUIRE(fixture.bus.registerCommand(
        late, [](const QJsonObject&) -> mikserstudio::core::CommandResult { return mikserstudio::core::CommandOutcome{}; }));
    REQUIRE(client.waitForEvent(u"command.registered"_s));
    CHECK(client.find(u"command.registered"_s).value(u"id"_s).toString() == u"test.late"_s);

    fixture.bus.unregisterCommand(u"test.late"_s);
    REQUIRE(client.waitForEvent(u"command.unregistered"_s));
    CHECK(client.find(u"command.unregistered"_s).value(u"id"_s).toString() == u"test.late"_s);
}

TEST_CASE("The events socket rejects missing or wrong tokens and other paths", "[api][events][auth]")
{
    ApiFixture fixture;

    SECTION("no token")
    {
        EventClient client;
        CHECK_FALSE(client.open(QNetworkRequest(QUrl(fixture.server->eventsUrl()))));
    }
    SECTION("wrong token")
    {
        EventClient client;
        CHECK_FALSE(client.open(queryRequest(fixture, u"not-the-token-0123456789"_s)));
    }
    SECTION("other path")
    {
        EventClient client;
        QUrl url(fixture.server->baseUrl().replace(u"http"_s, u"ws"_s) + u"/v1/commands"_s);
        url.setQuery(u"token="_s + fixture.server->token());
        CHECK_FALSE(client.open(QNetworkRequest(url)));
    }
    CHECK(fixture.server->eventClientCount() == 0);
}

TEST_CASE("Stopping the server disconnects events clients", "[api][events]")
{
    ApiFixture fixture;
    EventClient client;
    REQUIRE(client.open(queryRequest(fixture, fixture.server->token())));
    REQUIRE(waitFor([&] { return fixture.server->eventClientCount() == 1; }));

    fixture.server->stop();
    CHECK(fixture.server->eventClientCount() == 0);
    CHECK(waitFor([&] { return client.socket.state() == QAbstractSocket::UnconnectedState; }));
}
