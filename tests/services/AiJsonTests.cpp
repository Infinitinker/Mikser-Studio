#include "AiJson.h"
#include "ServicesTestSupport.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;
using namespace mikserstudio::services;

namespace {

QJsonDocument parse(const char* text)
{
    return QJsonDocument::fromJson(QByteArray(text));
}

}  // namespace

TEST_CASE("Backends are parsed with their operations", "[services][json]")
{
    const auto backends = parseBackends(parse(R"([
        {"id": "builtin", "title": "Built-in", "available": true, "reason": "",
         "ops": [{"id": "video.upscale", "title": "Upscale", "description": "d",
                  "paramsSchema": {"type": "object"}}]},
        {"id": "ollama", "title": "Ollama", "available": false, "reason": "Start Ollama.", "ops": []}
    ])"));
    REQUIRE(backends);
    REQUIRE(backends->size() == 2);
    const AiBackendInfo& builtin = backends->at(0);
    CHECK(builtin.id == u"builtin"_s);
    CHECK(builtin.available);
    REQUIRE(builtin.operations.size() == 1);
    CHECK(builtin.operations[0].id == u"video.upscale"_s);
    CHECK(builtin.operations[0].paramsSchema.value(u"type").toString() == u"object"_s);
    CHECK_FALSE(backends->at(1).available);
    CHECK(backends->at(1).reason == u"Start Ollama."_s);

    const QJsonObject json = toJson(builtin);
    CHECK(json.value(u"ops").toArray().at(0).toObject().value(u"id").toString() == u"video.upscale"_s);
    CHECK(json.value(u"available").toBool());
}

TEST_CASE("Malformed backend lists are refused with a reason", "[services][json]")
{
    CHECK(parseBackends(parse(R"({"id": "x"})")).error() == u"the backend list is not a JSON array"_s);
    CHECK(parseBackends(parse(R"([{"title": "x"}])")).error() == u"a backend has no \"id\""_s);
    CHECK(parseBackends(parse(R"([{"id": "b", "ops": [{}]}])")).error() == u"backend \"b\": an operation has no \"id\""_s);
}

TEST_CASE("Jobs are parsed with outputs, provenance and a clamped progress", "[services][json]")
{
    const auto jobs = parseJobs(parse(R"([
        {"id": "job-1", "op": "video.upscale", "backend": "builtin", "state": "succeeded", "progress": 1.5,
         "message": "Done", "error": null,
         "result": {"outputs": [{"path": "C:/cache/a.mkv", "kind": "video"}],
                    "provenance": {"backend": "builtin", "createdAt": "2026-11-11T00:00:00Z"}, "width": 3840}},
        {"id": "job-2", "op": "text.generate", "state": "failed", "progress": -1, "error": "Ollama is down",
         "result": null}
    ])"));
    REQUIRE(jobs);
    REQUIRE(jobs->size() == 2);
    const AiJobInfo& done = jobs->at(0);
    CHECK(done.state == AiJobState::Succeeded);
    CHECK(done.progress == 1.0);
    REQUIRE(done.outputs.size() == 1);
    CHECK(done.outputs[0].path == u"C:/cache/a.mkv"_s);
    CHECK(done.outputs[0].kind == u"video"_s);
    CHECK(done.provenance.value(u"backend").toString() == u"builtin"_s);
    CHECK(done.result.value(u"width").toInt() == 3840);
    CHECK(jobs->at(1).state == AiJobState::Failed);
    CHECK(jobs->at(1).progress == 0.0);
    CHECK(jobs->at(1).error == u"Ollama is down"_s);
    CHECK(jobs->at(1).outputs.isEmpty());
}

TEST_CASE("Job states round-trip through their names", "[services][json]")
{
    for (AiJobState state : {AiJobState::Queued, AiJobState::Running, AiJobState::Succeeded, AiJobState::Failed,
                             AiJobState::Canceled}) {
        CHECK(aiJobStateFromString(toString(state)) == state);
    }
    CHECK_FALSE(aiJobStateFromString(u"paused"));
    CHECK(isFinished(AiJobState::Canceled));
    CHECK_FALSE(isFinished(AiJobState::Running));
    CHECK(parseJob(parse(R"({"id": "j", "state": "paused"})").object()).error() ==
          u"job j has the unknown state \"paused\""_s);
}
