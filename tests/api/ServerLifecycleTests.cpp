// The local API server's life: its state and restarts, fixed and taken ports, IPv6 loopback, and no
// listening on addresses other than loopback.
#include "ApiTestSupport.h"

#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

using namespace Qt::StringLiterals;
using mikserstudio::tests::ApiFixture;
using mikserstudio::tests::sendRequest;

TEST_CASE("The server reports its state and can be restarted", "[api][lifecycle]")
{
    ApiFixture fixture;
    mikserstudio::api::ApiServer& server = *fixture.server;
    REQUIRE(server.isRunning());
    const quint16 port = server.port();
    CHECK(server.baseUrl() == u"http://127.0.0.1:%1"_s.arg(port));
    CHECK(server.eventsUrl() == u"ws://127.0.0.1:%1/v1/events"_s.arg(port));

    const auto again = server.start();
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().contains(u"already running"_s));

    server.stop();
    CHECK_FALSE(server.isRunning());
    CHECK(server.port() == 0);
    CHECK(server.baseUrl().isEmpty());
    CHECK(sendRequest("GET", QUrl(u"http://127.0.0.1:%1/v1/health"_s.arg(port))).status == 0);
    server.stop();

    const auto restarted = server.start();
    REQUIRE(restarted.has_value());
    CHECK(sendRequest("GET", fixture.url(u"/v1/health"_s)).status == 200);
    CHECK(fixture.get(u"/v1/commands"_s).status == 200);
}

TEST_CASE("A fixed port is used, and a taken port is reported", "[api][lifecycle]")
{
    QTcpServer probe;
    REQUIRE(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 taken = probe.serverPort();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);

    mikserstudio::api::ApiServer blocked(
        bus, {.port = taken, .token = {}, .discoveryFile = dir.filePath(u"a.json"_s), .pluginsReady = {}});
    const auto failed = blocked.start();
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().contains(QString::number(taken)));
    CHECK_FALSE(QFileInfo::exists(dir.filePath(u"a.json"_s)));

    probe.close();
    mikserstudio::api::ApiServer fixed(
        bus, {.port = taken, .token = {}, .discoveryFile = dir.filePath(u"b.json"_s), .pluginsReady = {}});
    const auto started = fixed.start();
    REQUIRE(started.has_value());
    CHECK(*started == taken);
    CHECK(sendRequest("GET", QUrl(fixed.baseUrl() + u"/v1/health"_s)).status == 200);
}

TEST_CASE("The server answers on IPv6 loopback when available", "[api][lifecycle]")
{
    ApiFixture fixture;
    QTcpServer ipv6Probe;
    if (!ipv6Probe.listen(QHostAddress::LocalHostIPv6, 0))
        SKIP("IPv6 loopback is not available on this machine");
    ipv6Probe.close();

    const auto reply = sendRequest("GET", QUrl(u"http://[::1]:%1/v1/health"_s.arg(fixture.server->port())));
    CHECK(reply.status == 200);
}

TEST_CASE("The server does not listen on non-loopback addresses", "[api][lifecycle]")
{
    ApiFixture fixture;
    QList<QHostAddress> external;
    for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
        if (!address.isLoopback() && !address.isLinkLocal() && address.protocol() == QAbstractSocket::IPv4Protocol)
            external.append(address);
    }
    if (external.isEmpty())
        SKIP("This machine has no non-loopback IPv4 address");

    for (const QHostAddress& address : external) {
        QTcpSocket socket;
        socket.connectToHost(address, fixture.server->port());
        CHECK_FALSE(socket.waitForConnected(3000));
    }
}
