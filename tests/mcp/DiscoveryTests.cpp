// Finding and reading the discovery file, and naming tools after commands and describing them.
#include "McpTestSupport.h"

#include "Discovery.h"
#include "ToolList.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace mikserstudio::mcp;

namespace {

QString written(const QTemporaryDir& folder, const QByteArray& contents)
{
    const QString path = folder.filePath(u"api.json"_s);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(contents);
    return path;
}

}  // namespace

TEST_CASE("the discovery file gives the API's address and token", "[mcp]")
{
    QTemporaryDir folder;
    const std::optional<Discovery> discovery = readDiscovery(
        written(folder, R"({"apiVersion": 1, "baseUrl": "http://127.0.0.1:5", "eventsUrl": "ws://127.0.0.1:5/v1/events",
                    "pid": 1, "port": 5, "token": "secret"})"));
    REQUIRE(discovery);
    CHECK(discovery->baseUrl == QUrl(u"http://127.0.0.1:5"_s));
    CHECK(discovery->eventsUrl == QUrl(u"ws://127.0.0.1:5/v1/events"_s));
    CHECK(discovery->token == "secret");
}

TEST_CASE("a missing, broken or newer discovery file means the app is not running", "[mcp]")
{
    QTemporaryDir folder;
    CHECK_FALSE(readDiscovery(folder.filePath(u"api.json"_s)));
    CHECK_FALSE(readDiscovery(written(folder, "{not json")));
    CHECK_FALSE(readDiscovery(
        written(folder, R"({"apiVersion": 2, "baseUrl": "http://127.0.0.1:5", "eventsUrl": "ws://x", "token": "t"})")));
    CHECK_FALSE(readDiscovery(written(folder, R"({"apiVersion": 1, "baseUrl": "http://127.0.0.1:5"})")));
}

TEST_CASE("each instance has its own discovery file, unless MIKSERSTUDIO_DATA_DIR names one folder", "[mcp]")
{
    CHECK(discoveryFile(u"installed"_s).endsWith(u"/Mikser Studio/api.json"_s));
    CHECK(discoveryFile(u"dev"_s).endsWith(u"/Mikser Studio Dev/api.json"_s));
    CHECK(discoveryFile(u"e2e"_s).endsWith(u"/Mikser Studio E2E/api.json"_s));

    QTemporaryDir folder;
    qputenv("MIKSERSTUDIO_DATA_DIR", folder.path().toUtf8());
    CHECK(discoveryFile(u"dev"_s) == folder.filePath(u"api.json"_s));
    qunsetenv("MIKSERSTUDIO_DATA_DIR");
}

TEST_CASE("tool names keep the letters, digits, _ and - of the command's id", "[mcp]")
{
    CHECK(toolName(u"project.import_media"_s) == u"project_import_media"_s);
    CHECK(toolName(u"hello.say-hi"_s) == u"hello_say-hi"_s);
    CHECK(toolName(QString(80, u'a')).size() == 64);
}

TEST_CASE("a tool has the command's result schema and says when it reaches outside the app", "[mcp]")
{
    const QJsonObject resultSchema{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"jobId"_s, QJsonObject{{u"type"_s, u"string"_s}}}}},
        {u"required"_s, QJsonArray{u"jobId"_s}},
    };
    const QJsonObject run{
        {u"id"_s, u"ai.run"_s},
        {u"title"_s, u"Run AI operation"_s},
        {u"resultSchema"_s, resultSchema},
        {u"openWorld"_s, true},
    };
    const QJsonObject structured = toolFor(run, Results::Structured);
    CHECK(structured[u"outputSchema"_s].toObject() == resultSchema);
    CHECK(structured[u"annotations"_s][u"openWorldHint"_s] == true);
    // A client that gets results as text only gets no output schema, which would promise structured ones.
    CHECK_FALSE(toolFor(run, Results::TextOnly).contains(u"outputSchema"_s));

    const QJsonObject list = toolFor(QJsonObject{{u"id"_s, u"project.list"_s}, {u"readOnly"_s, true}},
                                     Results::Structured);
    CHECK_FALSE(list.contains(u"outputSchema"_s));
    CHECK(list[u"annotations"_s][u"openWorldHint"_s] == false);
}
