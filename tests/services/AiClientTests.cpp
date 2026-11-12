#include "ServicesTestSupport.h"
#include "mikserstudio/services/AiClient.h"

#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>

#include <optional>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeHttpServer;
using mikserstudio::tests::RecordedRequest;
using mikserstudio::tests::waitFor;

namespace {

QHttpServerResponse jsonResponse(const char* text, QHttpServerResponse::StatusCode status = QHttpServerResponse::StatusCode::Ok)
{
    return QHttpServerResponse("application/json", QByteArray(text), status);
}

// Calls `call` with a callback that stores the answer, then waits for it.
template <typename T, typename Call>
std::expected<T, QString> await(Call call)
{
    std::optional<std::expected<T, QString>> answer;
    QObject context;
    call(&context, [&answer](std::expected<T, QString> value) { answer = std::move(value); });
    REQUIRE(waitFor([&] { return answer.has_value(); }));
    return *answer;
}

struct ClientFixture {
    FakeHttpServer server;
    AiClient client;

    ClientFixture() { client.setEndpoint(server.url(), "tok"); }
};

}  // namespace

TEST_CASE("Without an endpoint every call fails asynchronously", "[services][client]")
{
    AiClient client;
    CHECK_FALSE(client.hasEndpoint());
    bool called = false;
    QObject context;
    client.health(&context, [&called](std::expected<QJsonObject, QString> answer) {
        called = true;
        CHECK(answer.error() == u"The AI service is not running."_s);
    });
    CHECK_FALSE(called);
    REQUIRE(waitFor([&] { return called; }));
}

TEST_CASE("Requests carry the bearer token and parse the backends", "[services][client]")
{
    ClientFixture fixture;
    fixture.server.setHandler([](const RecordedRequest&) {
        return jsonResponse(R"([{"id": "builtin", "title": "Built-in", "available": true, "reason": "",
                                 "ops": [{"id": "video.upscale", "title": "Upscale", "paramsSchema": {}}]}])");
    });
    const auto backends = await<QList<AiBackendInfo>>(
        [&](const QObject* context, auto done) { fixture.client.backends(context, std::move(done)); });
    REQUIRE(backends);
    REQUIRE(backends->size() == 1);
    CHECK(backends->at(0).operations.at(0).id == u"video.upscale"_s);

    REQUIRE(fixture.server.requests().size() == 1);
    const RecordedRequest& request = fixture.server.requests().at(0);
    CHECK(request.method == "GET");
    CHECK(request.path == u"/v1/backends"_s);
    CHECK(request.authorization == "Bearer tok");
}

TEST_CASE("Jobs are submitted, listed, read and canceled", "[services][client]")
{
    ClientFixture fixture;
    fixture.server.setHandler([](const RecordedRequest& request) {
        if (request.method == "POST")
            return jsonResponse(R"({"jobId": "job-1"})", QHttpServerResponse::StatusCode::Accepted);
        if (request.method == "DELETE")
            return jsonResponse(R"({"id": "job 1/x", "op": "text.generate", "state": "canceled"})");
        if (request.path == u"/v1/jobs"_s)
            return jsonResponse(R"([{"id": "job-1", "op": "video.upscale", "state": "running", "progress": 0.25}])");
        return jsonResponse(R"({"id": "job-1", "op": "video.upscale", "state": "queued"})");
    });

    const auto id = await<QString>([&](const QObject* context, auto done) {
        fixture.client.submitJob(u"video.upscale"_s, QJsonObject{{u"input"_s, u"C:/a.mp4"_s}}, context, std::move(done));
    });
    CHECK(id == u"job-1"_s);
    const RecordedRequest& post = fixture.server.requests().at(0);
    CHECK(post.method == "POST");
    CHECK(post.path == u"/v1/jobs"_s);
    CHECK(QJsonDocument::fromJson(post.body).object() ==
          QJsonObject{{u"op"_s, u"video.upscale"_s}, {u"params"_s, QJsonObject{{u"input"_s, u"C:/a.mp4"_s}}}});

    const auto jobs =
        await<QList<AiJobInfo>>([&](const QObject* context, auto done) { fixture.client.jobs(context, std::move(done)); });
    REQUIRE(jobs);
    CHECK(jobs->at(0).state == AiJobState::Running);
    CHECK(jobs->at(0).progress == 0.25);

    const auto job = await<AiJobInfo>(
        [&](const QObject* context, auto done) { fixture.client.job(u"job-1"_s, context, std::move(done)); });
    CHECK(job->state == AiJobState::Queued);
    CHECK(fixture.server.requests().last().path == u"/v1/jobs/job-1"_s);

    const auto canceled = await<AiJobInfo>(
        [&](const QObject* context, auto done) { fixture.client.cancelJob(u"job 1/x"_s, context, std::move(done)); });
    CHECK(canceled->state == AiJobState::Canceled);
    CHECK(fixture.server.requests().last().method == "DELETE");
    CHECK(fixture.server.requests().last().path == u"/v1/jobs/job%201%2Fx"_s);
}

TEST_CASE("Error answers become the sidecar's message", "[services][client]")
{
    ClientFixture fixture;
    SECTION("with an error body")
    {
        fixture.server.setHandler([](const RecordedRequest&) {
            return jsonResponse(R"({"error": {"code": "unavailable", "message": "Ollama is not running."}})",
                                QHttpServerResponse::StatusCode::Conflict);
        });
        const auto id = await<QString>([&](const QObject* context, auto done) {
            fixture.client.submitJob(u"text.generate"_s, {}, context, std::move(done));
        });
        CHECK(id.error() == u"Ollama is not running."_s);
    }
    SECTION("without one")
    {
        fixture.server.setHandler(
            [](const RecordedRequest&) { return QHttpServerResponse(QHttpServerResponse::StatusCode::BadGateway); });
        const auto health =
            await<QJsonObject>([&](const QObject* context, auto done) { fixture.client.health(context, std::move(done)); });
        CHECK(health.error() == u"The AI service answered GET /v1/health with HTTP 502."_s);
    }
    SECTION("with an answer that is not JSON")
    {
        fixture.server.setHandler([](const RecordedRequest&) { return jsonResponse("[1, 2"); });
        const auto jobs =
            await<QList<AiJobInfo>>([&](const QObject* context, auto done) { fixture.client.jobs(context, std::move(done)); });
        CHECK(jobs.error().startsWith(u"The AI service sent an answer Mikser Studio does not understand"_s));
    }
    SECTION("with JSON of the wrong shape")
    {
        fixture.server.setHandler([](const RecordedRequest&) { return jsonResponse(R"({"jobs": []})"); });
        const auto jobs =
            await<QList<AiJobInfo>>([&](const QObject* context, auto done) { fixture.client.jobs(context, std::move(done)); });
        CHECK(jobs.error() ==
              u"The AI service sent an answer Mikser Studio does not understand: the job list is not a JSON array."_s);
    }
}

TEST_CASE("A sidecar that is gone or silent is reported", "[services][client]")
{
    AiClient client;
    SECTION("nothing listens")
    {
        int port = 0;
        {
            QTcpServer probe;
            REQUIRE(probe.listen(QHostAddress::LocalHost, 0));
            port = probe.serverPort();
        }
        client.setEndpoint(QUrl(u"http://127.0.0.1:%1"_s.arg(port)), "tok");
        const auto health = await<QJsonObject>([&](const QObject* context, auto done) { client.health(context, std::move(done)); });
        CHECK(health.error().startsWith(u"The AI service did not answer ("_s));
    }
    SECTION("it accepts but never answers")
    {
        QTcpServer silent;
        REQUIRE(silent.listen(QHostAddress::LocalHost, 0));
        client.setEndpoint(QUrl(u"http://127.0.0.1:%1"_s.arg(silent.serverPort())), "tok");
        client.setTimeout(300ms);
        const auto health = await<QJsonObject>([&](const QObject* context, auto done) { client.health(context, std::move(done)); });
        CHECK(health.error() == u"The AI service did not answer (no answer within 0.3 seconds)."_s);
    }
}

TEST_CASE("Answers for a destroyed context are dropped", "[services][client]")
{
    ClientFixture fixture;
    fixture.server.setHandler([](const RecordedRequest&) { return jsonResponse(R"({"status": "ok"})"); });
    bool called = false;
    {
        QObject context;
        fixture.client.health(&context, [&called](std::expected<QJsonObject, QString>) { called = true; });
    }
    REQUIRE(waitFor([&] { return !fixture.server.requests().isEmpty(); }));
    mikserstudio::tests::spin(200ms);
    CHECK_FALSE(called);
}
