// The built mikser-studio-mcp: it speaks MCP on stdin and stdout, logs to stderr, and ends with its input.
#include "FakeStudio.h"
#include "McpTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

QByteArray line(const QJsonObject& message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QList<QJsonObject> answersIn(const QByteArray& output)
{
    QList<QJsonObject> answers;
    for (const QByteArray& written : output.split('\n')) {
        if (!written.trimmed().isEmpty())
            answers.append(QJsonDocument::fromJson(written).object());
    }
    return answers;
}

const QJsonObject kInitialize{{u"jsonrpc"_s, u"2.0"_s},
                              {u"id"_s, 1},
                              {u"method"_s, u"initialize"_s},
                              {u"params"_s, QJsonObject{{u"protocolVersion"_s, u"2025-06-18"_s}}}};
const QJsonObject kInitialized{{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}};
const QJsonObject kListTools{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, 2}, {u"method"_s, u"tools/list"_s}};

}  // namespace

TEST_CASE("the program speaks MCP on stdin and stdout and ends with its input", "[mcp]")
{
    QTemporaryDir folder;
    QProcess program;
    program.start(QStringLiteral(MIKSERSTUDIO_MCP), {u"--discovery-file"_s, folder.filePath(u"api.json"_s)});
    REQUIRE(program.waitForStarted());
    program.write(line(kInitialize) + line(kInitialized) + line(kListTools));
    program.closeWriteChannel();
    REQUIRE(program.waitForFinished(30'000));

    const QString log = QString::fromUtf8(program.readAllStandardError());
    INFO(log.toStdString());
    CHECK(program.exitStatus() == QProcess::NormalExit);
    CHECK(program.exitCode() == 0);
    const QList<QJsonObject> answers = answersIn(program.readAllStandardOutput());
    REQUIRE(answers.size() == 2);
    CHECK(answers[0][u"id"_s] == 1);
    CHECK(answers[1][u"id"_s] == 2);
    CHECK(answers[1][u"result"_s].toObject() == QJsonObject{{u"tools"_s, QJsonArray{}}});
    CHECK(log.contains(u"Looking for Mikser Studio"_s));
}

TEST_CASE("the program runs a command of the app it finds", "[mcp]")
{
    QTemporaryDir folder;
    mikserstudio::tests::FakeStudio studio(folder.filePath(u"api.json"_s));
    QProcess program;
    program.start(QStringLiteral(MIKSERSTUDIO_MCP), {u"--discovery-file"_s, folder.filePath(u"api.json"_s)});
    REQUIRE(program.waitForStarted());
    const QJsonObject call{{u"jsonrpc"_s, u"2.0"_s},
                           {u"id"_s, 3},
                           {u"method"_s, u"tools/call"_s},
                           {u"params"_s, QJsonObject{{u"name"_s, u"project_list"_s}}}};
    program.write(line(kInitialize) + line(call));
    // The fake answers on this thread's event loop, so the test waits on it rather than on the process.
    QByteArray output;
    REQUIRE(mikserstudio::tests::waitFor([&] {
        output += program.readAllStandardOutput();
        return output.count('\n') >= 2;
    }));
    program.closeWriteChannel();
    REQUIRE(mikserstudio::tests::waitFor([&] { return program.state() == QProcess::NotRunning; }));

    const QList<QJsonObject> answers = answersIn(output);
    REQUIRE(answers.size() == 2);
    CHECK(answers[1][u"id"_s] == 3);
    CHECK(answers[1][u"result"_s][u"structuredContent"_s].toObject() == QJsonObject{{u"ran"_s, u"project.list"_s}});
    CHECK(program.exitCode() == 0);
}

TEST_CASE("the program refuses an instance it does not know", "[mcp]")
{
    QProcess program;
    program.start(QStringLiteral(MIKSERSTUDIO_MCP), {u"--instance"_s, u"nightly"_s});
    program.closeWriteChannel();
    REQUIRE(program.waitForFinished(30'000));
    CHECK(program.exitCode() == 2);
    CHECK(QString::fromUtf8(program.readAllStandardError()).contains(u"installed, dev, e2e"_s));
}
