// The http.request and http.stream_ndjson job kinds against an HTTP server in the test process: their
// answers and errors, the network entries and the app's own API, cancel, the silence timeout and
// what happens when they go.
#include "NetworkAccess.h"
#include "ServicesTestSupport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/services/HttpJobs.h"

#include <QHttpHeaders>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeHttpServer;
using mikserstudio::tests::RecordedRequest;
using mikserstudio::tests::waitFor;

namespace {

QHttpServerResponse text(const QByteArray& mime, const QByteArray& body,
                         QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::Ok)
{
    return {mime, body, status};
}

struct HttpFixture {
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::JobManager jobs{bus};
    std::optional<quint16> apiPort;
    std::unique_ptr<HttpJobs> http = std::make_unique<HttpJobs>(jobs,
                                                                HttpJobOptions{.apiPort = [this] { return apiPort; }});
    FakeHttpServer server;
    QStringList network{u"127.0.0.1:*"_s};

    [[nodiscard]] QString url(const QString& path) const { return server.url().toString() + path; }

    std::expected<QString, core::CommandError> start(const QString& kind, const QJsonObject& input,
                                                     const QStringList& permissions = core::allPermissions())
    {
        return jobs.start(
            kind, input,
            core::CallContext{.origins = {u"plugin:test.plugin"_s}, .permissions = permissions, .confirmed = false},
            core::JobRequest{.parent = {}, .network = network});
    }

    QJsonObject job(const QString& id) const
    {
        for (const QJsonValue value : jobs.jobsJson()) {
            if (value.toObject().value(u"id").toString() == id)
                return value.toObject();
        }
        return {};
    }

    QJsonObject finished(const QString& id) const
    {
        REQUIRE(waitFor([&] { return jobs.state(id).value_or(core::JobState::Running) > core::JobState::Running; }));
        return job(id);
    }

    QJsonObject run(const QString& kind, const QJsonObject& input)
    {
        const auto id = start(kind, input);
        if (!id)
            FAIL("start failed: " << id.error().message.toStdString());
        return finished(*id);
    }
};

// A server that takes connections and never answers.
struct SilentServer {
    QTcpServer server;
    QList<QTcpSocket*> sockets;

    SilentServer()
    {
        REQUIRE(server.listen(QHostAddress::LocalHost));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket* socket = server.nextPendingConnection())
                sockets.append(socket);
        });
    }

    [[nodiscard]] QString url() const { return u"http://127.0.0.1:%1/slow"_s.arg(server.serverPort()); }
    // Whether a request has arrived: only then is it on its connection, which canceling closes. A
    // request canceled before it was sent leaves its connection open for the next one.
    [[nodiscard]] bool requested() const
    {
        return std::ranges::any_of(sockets, [](const QTcpSocket* socket) { return socket->bytesAvailable() > 0; });
    }
    [[nodiscard]] bool allClosed() const
    {
        return std::ranges::all_of(
            sockets, [](const QTcpSocket* socket) { return socket->state() != QTcpSocket::ConnectedState; });
    }
};

}  // namespace

TEST_CASE("http.request answers with the status and the body, also for an error answer", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([](const RecordedRequest& request) {
        if (request.path == u"/ok")
            return text("application/json", R"({"answer":42})");
        return text("text/plain", "no such thing", QHttpServerResponse::StatusCode::NotFound);
    });

    const QJsonObject ok = f.run(u"http.request"_s, {{u"url"_s, f.url(u"/ok"_s)}});
    REQUIRE(ok.value(u"state").toString() == u"succeeded"_s);
    const QJsonObject result = ok.value(u"result").toObject();
    CHECK(result.value(u"status").toInt() == 200);
    CHECK(result.value(u"body").toString() == uR"({"answer":42})"_s);
    CHECK(result.value(u"json").toObject().value(u"answer").toInt() == 42);
    CHECK(ok.value(u"owner").toString() == u"test.plugin"_s);
    CHECK(ok.value(u"title").toString() == u"GET %1"_s.arg(f.url(u"/ok"_s)));

    const QJsonObject missing = f.run(u"http.request"_s,
                                      {{u"url"_s, f.url(u"/missing"_s)}, {u"title"_s, u"Look it up"_s}});
    REQUIRE(missing.value(u"state").toString() == u"succeeded"_s);
    CHECK(missing.value(u"result").toObject().value(u"status").toInt() == 404);
    CHECK(missing.value(u"result").toObject().value(u"body").toString() == u"no such thing"_s);
    CHECK_FALSE(missing.value(u"result").toObject().contains(u"json"));
    CHECK(missing.value(u"title").toString() == u"Look it up"_s);
}

TEST_CASE("http.request sends its body as JSON with its method", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([](const RecordedRequest&) { return text("application/json", "{}"); });
    const QJsonObject job = f.run(u"http.request"_s, {{u"url"_s, f.url(u"/api/show"_s)},
                                                      {u"method"_s, u"POST"_s},
                                                      {u"body"_s, QJsonObject{{u"model"_s, u"llama3"_s}}}});
    CHECK(job.value(u"state").toString() == u"succeeded"_s);
    REQUIRE(f.server.requests().size() == 1);
    CHECK(f.server.requests().first().method == "POST");
    CHECK(QJsonDocument::fromJson(f.server.requests().first().body).object().value(u"model").toString() == u"llama3"_s);
}

TEST_CASE("A redirect is answered, not followed", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([&f](const RecordedRequest& request) {
        if (request.path == u"/elsewhere")
            return text("text/plain", "followed");
        QHttpServerResponse response(QHttpServerResponse::StatusCode::Found);
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::Location, f.url(u"/elsewhere"_s));
        response.setHeaders(headers);
        return response;
    });
    const QJsonObject job = f.run(u"http.request"_s, {{u"url"_s, f.url(u"/start"_s)}});
    CHECK(job.value(u"result").toObject().value(u"status").toInt() == 302);
    CHECK(f.server.requests().size() == 1);
}

TEST_CASE("The jobs reach only what the network entries allow, and never the app's API", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([](const RecordedRequest&) { return text("text/plain", "hi"); });
    const QJsonObject input{{u"url"_s, f.url(u"/"_s)}};

    SECTION("An entry that names the port, or localhost, reaches this computer")
    {
        for (const QString& entry : {u"127.0.0.1:%1"_s.arg(f.server.port()), u"localhost:*"_s, u"[::1]:*"_s}) {
            f.network = {entry};
            CHECK(f.start(u"http.request"_s, input).has_value());
        }
        CHECK(waitFor([&] { return f.server.requests().size() == 3; }));
    }
    SECTION("No entry, another port or any other computer don't")
    {
        for (const QStringList& entries :
             {QStringList{}, QStringList{u"127.0.0.1:1"_s}, QStringList{u"*:*"_s}, QStringList{u"example.com:*"_s}}) {
            f.network = entries;
            const auto refused = f.start(u"http.request"_s, input);
            REQUIRE_FALSE(refused.has_value());
            CHECK(refused.error().code == core::CommandErrorCode::PermissionDenied);
            CHECK(refused.error().message.contains(u"may not connect to 127.0.0.1:%1"_s.arg(f.server.port())));
        }
    }
    SECTION("The app's own API is refused whatever the entries say")
    {
        f.apiPort = static_cast<quint16>(f.server.port());
        f.network = {u"127.0.0.1:*"_s, u"*:*"_s};
        const auto refused = f.start(u"http.request"_s, input);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().message.contains(u"Mikser Studio’s own API"_s));
    }
    SECTION("Without the network permission nothing starts")
    {
        const auto refused = f.start(u"http.request"_s, input, {u"project.read"_s});
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == core::CommandErrorCode::PermissionDenied);
    }
}

TEST_CASE("Addresses on this computer are told apart from others", "[services][http]")
{
    for (const QString& host : {u"localhost"_s, u"LocalHost"_s, u"app.localhost"_s, u"127.0.0.1"_s, u"127.8.9.10"_s,
                                u"::1"_s, u"::ffff:127.0.0.1"_s, u"0.0.0.0"_s, u"::"_s}) {
        INFO(host.toStdString());
        CHECK(isThisComputer(host));
    }
    for (const QString& host : {u"example.com"_s, u"192.168.1.20"_s, u"10.0.0.1"_s, u"fe80::1"_s, u"localhost.com"_s}) {
        INFO(host.toStdString());
        CHECK_FALSE(isThisComputer(host));
    }
    // Short and numeric forms of 127.0.0.1 count as this computer too.
    for (const QString& url : {u"http://2130706433:80/"_s, u"http://127.1/"_s, u"http://0x7f.0.0.1/"_s}) {
        INFO(url.toStdString());
        CHECK_FALSE(checkNetworkAccess({u"*:*"_s}, QUrl(url), std::nullopt).has_value());
    }
    CHECK(checkNetworkAccess({u"*:443"_s}, QUrl(u"https://api.example.com/v1"_s), std::nullopt).has_value());
    CHECK_FALSE(checkNetworkAccess({u"*:443"_s}, QUrl(u"http://api.example.com/v1"_s), std::nullopt).has_value());
    CHECK(checkNetworkAccess({u"api.example.com:80"_s}, QUrl(u"http://API.example.com/"_s), std::nullopt).has_value());
}

TEST_CASE("Input that doesn't fit is refused before anything is sent", "[services][http]")
{
    HttpFixture f;
    for (const QJsonObject& input :
         {QJsonObject{}, QJsonObject{{u"url"_s, f.url(u"/"_s)}, {u"method"_s, u"BREW"_s}},
          QJsonObject{{u"url"_s, f.url(u"/"_s)}, {u"extra"_s, true}}, QJsonObject{{u"url"_s, u"ftp://127.0.0.1/"_s}},
          QJsonObject{{u"url"_s, u"not an address"_s}}}) {
        const auto refused = f.start(u"http.request"_s, input);
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().code == core::CommandErrorCode::InvalidParams);
    }
    CHECK(f.server.requests().isEmpty());
}

TEST_CASE("A request that can't connect, or gets no answer in time, fails with the reason", "[services][http]")
{
    HttpFixture f;
    const QJsonObject closed = f.run(u"http.request"_s, {{u"url"_s, u"http://127.0.0.1:9/"_s}});
    CHECK(closed.value(u"state").toString() == u"failed"_s);
    CHECK(closed.value(u"error").toString().startsWith(u"Could not reach http://127.0.0.1:9/"_s));

    SilentServer silent;
    const QJsonObject slow = f.run(u"http.request"_s, {{u"url"_s, silent.url()}, {u"timeout"_s, 0.3}});
    CHECK(slow.value(u"state").toString() == u"failed"_s);
    CHECK(slow.value(u"error").toString() == u"%1 sent nothing for 0.3 s."_s.arg(silent.url()));
}

TEST_CASE("An answer larger than the limit fails the job", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler(
        [](const RecordedRequest&) { return text("text/plain", QByteArray(HttpJobs::kMaxAnswerBytes + 1024, 'a')); });
    const QJsonObject job = f.run(u"http.request"_s, {{u"url"_s, f.url(u"/big"_s)}});
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().contains(u"larger than 16 MB"_s));
}

TEST_CASE("http.stream_ndjson joins the fields' text and shows its progress", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([](const RecordedRequest&) {
        return text("application/x-ndjson", R"({"thinking":"Hm","response":""})"
                                            "\n"
                                            R"({"response":" Hello","done":false})"
                                            "\n\n"
                                            R"({"response":" there.","done":true,"done_reason":"stop"})");
    });
    const QJsonObject job = f.run(u"http.stream_ndjson"_s,
                                  {{u"url"_s, f.url(u"/api/generate"_s)},
                                   {u"body"_s, QJsonObject{{u"prompt"_s, u"Hi"_s}}},
                                   {u"fields"_s, QJsonArray{u"response"_s, u"thinking"_s}},
                                   {u"messages"_s, QJsonObject{{u"response"_s, u"Writing ({count})"_s},
                                                               {u"thinking"_s, u"Thinking ({count})"_s}}}});
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    const QJsonObject result = job.value(u"result").toObject();
    CHECK(result.value(u"fields").toObject().value(u"response").toString() == u" Hello there."_s);
    CHECK(result.value(u"fields").toObject().value(u"thinking").toString() == u"Hm"_s);
    CHECK(result.value(u"last").toObject().value(u"done_reason").toString() == u"stop"_s);
    CHECK(result.value(u"lines").toInt() == 3);
    CHECK(job.value(u"message").toString() == u"Writing (3)"_s);
    CHECK(f.server.requests().first().method == "POST");
}

TEST_CASE("An error line or an error answer fails the stream with the tool's message", "[services][http]")
{
    HttpFixture f;
    f.server.setHandler([](const RecordedRequest& request) {
        if (request.path == u"/line")
            return text("application/x-ndjson", R"({"response":"Hel"})"
                                                "\n"
                                                R"({"error":"model runner has unexpectedly stopped"})"
                                                "\n"
                                                R"({"response":"lo"})");
        return text("application/json", R"({"error":"model 'nope' not found"})",
                    QHttpServerResponse::StatusCode::NotFound);
    });
    const QJsonObject line = f.run(u"http.stream_ndjson"_s, {{u"url"_s, f.url(u"/line"_s)}});
    CHECK(line.value(u"state").toString() == u"failed"_s);
    CHECK(line.value(u"error").toString() == u"model runner has unexpectedly stopped"_s);

    const QJsonObject answer = f.run(u"http.stream_ndjson"_s, {{u"url"_s, f.url(u"/answer"_s)}});
    CHECK(answer.value(u"state").toString() == u"failed"_s);
    CHECK(answer.value(u"error").toString() == u"model 'nope' not found"_s);
}

TEST_CASE("Canceling a job aborts its request", "[services][http]")
{
    HttpFixture f;
    SilentServer silent;
    const auto id = f.start(u"http.stream_ndjson"_s, {{u"url"_s, silent.url()}});
    REQUIRE(id);
    REQUIRE(waitFor([&] { return silent.requested(); }));
    REQUIRE(f.jobs.cancel(*id) == core::JobState::Canceled);
    CHECK(waitFor([&] { return silent.allClosed(); }));
    CHECK(f.job(*id).value(u"state").toString() == u"canceled"_s);
}

TEST_CASE("When the HTTP jobs go, their kinds go and their requests fail", "[services][http]")
{
    HttpFixture f;
    SilentServer silent;
    const auto id = f.start(u"http.request"_s, {{u"url"_s, silent.url()}});
    REQUIRE(id);
    REQUIRE(waitFor([&] { return silent.requested(); }));
    f.http.reset();
    CHECK(f.job(*id).value(u"state").toString() == u"failed"_s);
    CHECK(f.job(*id).value(u"error").toString().contains(u"closing"_s));
    CHECK(waitFor([&] { return silent.allClosed(); }));
    const auto gone = f.start(u"http.request"_s, {{u"url"_s, silent.url()}});
    REQUIRE_FALSE(gone.has_value());
    CHECK(gone.error().code == core::CommandErrorCode::InvalidParams);
}
