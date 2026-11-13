// The MCP bridge against a fake Mikser Studio: the tools it lists, running them, asking the person before
// commands that replace their work, following the app as it starts and stops, and the recent events.
#include "FakeStudio.h"
#include "McpTestSupport.h"

#include "AppClient.h"
#include "Bridge.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QJsonArray>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace mikserstudio;
using tests::Client;
using tests::FakeStudio;

namespace {

// A discovery file's path in a folder of the test's own.
struct Folder {
    QTemporaryDir dir;
    QString discoveryPath = dir.filePath(u"api.json"_s);
};

QString textOf(const QJsonObject& result)
{
    return result[u"content"_s][0][u"text"_s].toString();
}

QJsonObject parsed(const QString& text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QString methodOf(const QJsonObject& message)
{
    return message[u"method"_s].toString();
}

}  // namespace

TEST_CASE("initialize offers tools that can change", "[mcp]")
{
    Folder folder;
    Client client(folder.discoveryPath);
    const QJsonObject result = client
                                   .request(u"initialize"_s, {{u"protocolVersion"_s, u"2025-06-18"_s},
                                                              {u"capabilities"_s, QJsonObject{}}})[u"result"_s]
                                   .toObject();
    CHECK(result[u"protocolVersion"_s] == u"2025-06-18"_s);
    CHECK(result[u"capabilities"_s].toObject() == QJsonObject{{u"tools"_s, QJsonObject{{u"listChanged"_s, true}}}});
    CHECK(result[u"serverInfo"_s][u"name"_s] == u"mikser-studio"_s);
    CHECK(result[u"instructions"_s].toString().contains(u"project_list"_s));
    CHECK(result[u"instructions"_s].toString().contains(u"recent_events"_s));
}

TEST_CASE("initialize answers an unknown version with the newest", "[mcp]")
{
    Folder folder;
    Client client(folder.discoveryPath);
    const QJsonObject answer = client.request(
        u"initialize"_s, {{u"protocolVersion"_s, u"1999-01-01"_s}, {u"capabilities"_s, QJsonObject{}}});
    CHECK(answer[u"result"_s][u"protocolVersion"_s] == mcp::protocolVersions().constFirst());
}

TEST_CASE("commands are listed as tools with their hints", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start();
    const QHash<QString, QJsonObject> tools = client.tools();
    CHECK(tools.size() == 5);
    REQUIRE(tools.contains(u"edit_undo"_s));
    REQUIRE(tools.contains(u"recent_events"_s));

    const QJsonObject listing = tools.value(u"project_list"_s);
    CHECK(listing[u"annotations"_s][u"readOnlyHint"_s] == true);
    CHECK(listing[u"annotations"_s][u"destructiveHint"_s] == false);
    CHECK(listing[u"inputSchema"_s].toObject() ==
          QJsonObject{{u"type"_s, u"object"_s}, {u"properties"_s, QJsonObject{}}, {u"additionalProperties"_s, false}});

    const QJsonObject imported = tools.value(u"project_import_media"_s);
    CHECK(imported[u"title"_s] == u"Import media…"_s);
    CHECK(imported[u"inputSchema"_s][u"required"_s].toArray() == QJsonArray{u"path"_s});
    CHECK(imported[u"description"_s].toString().contains(u"can be undone"_s));

    const QJsonObject created = tools.value(u"project_new"_s);
    CHECK(created[u"annotations"_s][u"destructiveHint"_s] == true);
    CHECK(created[u"description"_s].toString().contains(u"Unsaved changes will be lost"_s));

    CHECK(tools.value(u"recent_events"_s)[u"annotations"_s][u"readOnlyHint"_s] == true);
}

TEST_CASE("a command whose tool name is the bridge's own is left out", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    studio.commands.append(QJsonObject{{u"id"_s, u"recent.events"_s}, {u"title"_s, u"Taken"_s}});
    Client client(folder.discoveryPath);
    client.start();
    CHECK(client.tools().value(u"recent_events"_s)[u"title"_s] == u"Recent events"_s);
}

TEST_CASE("a tool runs its command with the arguments", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start();
    const QJsonObject result = client.call(u"project_import_media"_s, {{u"path"_s, u"D:/clip.mp4"_s}});
    REQUIRE(studio.runs.size() == 1);
    CHECK(studio.runs[0].first == u"project.import_media"_s);
    CHECK(studio.runs[0].second == QJsonObject{{u"path"_s, u"D:/clip.mp4"_s}});
    CHECK(result[u"structuredContent"_s].toObject() == QJsonObject{{u"ran"_s, u"project.import_media"_s}});
    CHECK(parsed(textOf(result)) == QJsonObject{{u"ran"_s, u"project.import_media"_s}});
    CHECK_FALSE(result.contains(u"isError"_s));
}

TEST_CASE("older clients get the result as text only", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start({}, u"2025-03-26"_s);
    const QJsonObject result = client.call(u"project_list"_s);
    CHECK_FALSE(result.contains(u"structuredContent"_s));
    CHECK(parsed(textOf(result)) == QJsonObject{{u"ran"_s, u"project.list"_s}});
}

TEST_CASE("a failed command is a tool error with the app's message", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start();
    const QJsonObject result = client.call(u"edit_undo"_s);
    CHECK(result[u"isError"_s] == true);
    CHECK(textOf(result) == u"There is nothing to undo. (not_available)"_s);
}

TEST_CASE("an unknown tool is invalid params", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start();
    const QJsonObject answer = client.request(
        u"tools/call"_s, {{u"name"_s, u"project_delete_everything"_s}, {u"arguments"_s, QJsonObject{}}});
    CHECK(answer[u"error"_s][u"code"_s] == mcp::InvalidParams);
    CHECK(answer[u"error"_s][u"message"_s].toString().contains(u"List the tools again"_s));
}

TEST_CASE("without the app there are no tools and calls say to start it", "[mcp]")
{
    Folder folder;
    Client client(folder.discoveryPath);
    client.start();
    CHECK(client.tools().isEmpty());
    const QJsonObject result = client.call(u"project_list"_s);
    CHECK(result[u"isError"_s] == true);
    CHECK(textOf(result) == mcp::notRunningMessage());
}

TEST_CASE("a command with a confirmation does not run when the client cannot ask", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start();
    const QJsonObject result = client.call(u"project_new"_s);
    CHECK(result[u"isError"_s] == true);
    CHECK(textOf(result).contains(u"Start a new project? Unsaved changes will be lost."_s));
    CHECK(studio.runs.isEmpty());
}

TEST_CASE("a command with a confirmation runs only when the person agrees", "[mcp]")
{
    const auto [action, runs] = GENERATE(table<const char*, qsizetype>({{"accept", 1}, {"decline", 0}, {"cancel", 0}}));
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start({{u"elicitation"_s, QJsonObject{}}});
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"id"_s, 99},
                 {u"method"_s, u"tools/call"_s},
                 {u"params"_s, QJsonObject{{u"name"_s, u"project_new"_s}}}});

    const QJsonObject asked = client.nextMessage();
    REQUIRE(methodOf(asked) == u"elicitation/create"_s);
    CHECK(asked[u"params"_s][u"message"_s] == u"Start a new project? Unsaved changes will be lost."_s);
    CHECK(studio.runs.isEmpty());
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"id"_s, asked[u"id"_s]},
                 {u"result"_s, QJsonObject{{u"action"_s, QString::fromLatin1(action)}}}});

    const QJsonObject answer = client.nextMessage();
    CHECK(answer[u"id"_s] == 99);
    CHECK(studio.runs.size() == runs);
    CHECK(answer[u"result"_s][u"isError"_s].toBool() == (runs == 0));
}

TEST_CASE("a canceled request gets no answer", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start({{u"elicitation"_s, QJsonObject{}}});
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"id"_s, 7},
                 {u"method"_s, u"tools/call"_s},
                 {u"params"_s, QJsonObject{{u"name"_s, u"project_new"_s}}}});
    const QJsonObject asked = client.nextMessage();
    REQUIRE(methodOf(asked) == u"elicitation/create"_s);
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"method"_s, u"notifications/cancelled"_s},
                 {u"params"_s, QJsonObject{{u"requestId"_s, 7}}}});
    client.send(
        {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, asked[u"id"_s]}, {u"result"_s, QJsonObject{{u"action"_s, u"accept"_s}}}});
    CHECK(client.request(u"ping"_s)[u"id"_s] == 2);
    CHECK(studio.runs.isEmpty());
}

TEST_CASE("the client hears when the app starts, stops and changes its commands", "[mcp]")
{
    Folder folder;
    Client client(folder.discoveryPath, {.retry = std::chrono::milliseconds(50), .closeTimeout = {}});
    client.start();
    client.bridge().start();

    auto studio = std::make_unique<FakeStudio>(folder.discoveryPath);
    CHECK(methodOf(client.nextMessage()) == u"notifications/tools/list_changed"_s);

    REQUIRE(tests::waitFor([&] { return studio->openSockets() == 1; }));
    studio->commands.append(QJsonObject{{u"id"_s, u"hello.greet"_s}, {u"title"_s, u"Greet"_s}});
    studio->sendEvent({{u"type"_s, u"command.registered"_s}, {u"id"_s, u"hello.greet"_s}});
    CHECK(methodOf(client.nextMessage()) == u"notifications/tools/list_changed"_s);
    CHECK(client.tools().contains(u"hello_greet"_s));

    studio.reset();
    CHECK(methodOf(client.nextMessage()) == u"notifications/tools/list_changed"_s);
}

TEST_CASE("recent_events lists the app's events, and those after a number", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath, {.retry = std::chrono::milliseconds(50), .closeTimeout = {}});
    client.start();
    client.bridge().start();
    CHECK(methodOf(client.nextMessage()) == u"notifications/tools/list_changed"_s);
    REQUIRE(tests::waitFor([&] { return studio.openSockets() == 1; }));

    studio.sendEvent({{u"type"_s, u"command.executed"_s}, {u"id"_s, u"timeline.add_clip"_s}});
    studio.sendEvent({{u"type"_s, u"undo.changed"_s}, {u"canUndo"_s, true}});
    QJsonObject events;
    REQUIRE(tests::waitFor([&] {
        events = client.call(u"recent_events"_s)[u"structuredContent"_s].toObject();
        return events[u"latest"_s].toInt() == 2;
    }));
    const QJsonArray all = events[u"events"_s].toArray();
    REQUIRE(all.size() == 2);
    CHECK(all[0][u"number"_s] == 1);
    CHECK(all[0][u"event"_s][u"id"_s] == u"timeline.add_clip"_s);
    CHECK(!all[0][u"time"_s].toString().isEmpty());

    const QJsonObject later = client.call(u"recent_events"_s, {{u"after"_s, 1}})[u"structuredContent"_s].toObject();
    REQUIRE(later[u"events"_s].toArray().size() == 1);
    CHECK(later[u"events"_s][0][u"event"_s][u"type"_s] == u"undo.changed"_s);

    const QJsonObject wrong = client.request(
        u"tools/call"_s, {{u"name"_s, u"recent_events"_s}, {u"arguments"_s, QJsonObject{{u"after"_s, -1}}}});
    CHECK(wrong[u"error"_s][u"code"_s] == mcp::InvalidParams);
}

TEST_CASE("unknown methods and broken messages get JSON-RPC errors", "[mcp]")
{
    Folder folder;
    Client client(folder.discoveryPath);
    CHECK(client.request(u"resources/list"_s)[u"error"_s][u"code"_s] == mcp::MethodNotFound);
    client.bridge().receive("{not json");
    CHECK(client.nextMessage()[u"error"_s][u"code"_s] == mcp::ParseError);
    client.bridge().receive("[1, 2]");
    CHECK(client.nextMessage()[u"error"_s][u"code"_s] == mcp::InvalidRequest);
}

TEST_CASE("closing answers open questions as declined and then ends", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start({{u"elicitation"_s, QJsonObject{}}});
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"id"_s, 5},
                 {u"method"_s, u"tools/call"_s},
                 {u"params"_s, QJsonObject{{u"name"_s, u"project_new"_s}}}});
    REQUIRE(methodOf(client.nextMessage()) == u"elicitation/create"_s);

    bool closed = false;
    QObject::connect(&client.bridge(), &mcp::Bridge::closed, [&closed] { closed = true; });
    client.bridge().close();
    const QJsonObject answer = client.nextMessage();
    CHECK(answer[u"id"_s] == 5);
    CHECK(answer[u"result"_s][u"isError"_s] == true);
    CHECK(closed);
    CHECK(studio.runs.isEmpty());
}

TEST_CASE("a confirmation due after the client has gone is declined at once", "[mcp]")
{
    Folder folder;
    FakeStudio studio(folder.discoveryPath);
    Client client(folder.discoveryPath);
    client.start({{u"elicitation"_s, QJsonObject{}}});
    // The bridge asks for the commands first; the client goes before they arrive.
    client.send({{u"jsonrpc"_s, u"2.0"_s},
                 {u"id"_s, 6},
                 {u"method"_s, u"tools/call"_s},
                 {u"params"_s, QJsonObject{{u"name"_s, u"project_new"_s}}}});
    bool closed = false;
    QObject::connect(&client.bridge(), &mcp::Bridge::closed, [&closed] { closed = true; });
    client.bridge().close();

    const QJsonObject answer = client.nextMessage();
    CHECK(answer[u"id"_s] == 6);
    CHECK(answer[u"result"_s][u"isError"_s] == true);
    CHECK(closed);
    CHECK(studio.runs.isEmpty());
}
