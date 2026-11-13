// End to end: the built mikser-studio-mcp drives the built mikser-studio, run headless as the E2E
// instance, as Claude Code drives it. The bridge finds the app when it starts, lists its commands as tools,
// runs them, asks before a command that replaces the person's work, shows the app's recent events, hears
// the app stop, and ends when its client closes stdin.
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

constexpr int kStartTimeoutMs = 60'000;
constexpr int kStopTimeoutMs = 30'000;
constexpr int kAnswerTimeoutMs = 20'000;

// Where the E2E instance writes its discovery file: main.cpp names the tests like that instance.
QString discoveryFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/api.json"_s;
}

// mikser-studio-mcp --instance e2e, and an MCP client of it on its stdin and stdout.
class McpClient {
public:
    McpClient()
    {
        m_process.start(QStringLiteral(MIKSERSTUDIO_MCP), {u"--instance"_s, u"e2e"_s});
        m_process.waitForStarted();
    }

    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;
    McpClient(McpClient&&) = delete;
    McpClient& operator=(McpClient&&) = delete;

    ~McpClient()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(kStopTimeoutMs);
        }
    }

    void send(const QJsonObject& message)
    {
        m_process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        m_process.waitForBytesWritten();
    }

    // The next message the bridge writes, or an empty object when none comes in time.
    QJsonObject next()
    {
        QElapsedTimer timer;
        timer.start();
        while (!m_process.canReadLine() && timer.elapsed() < kAnswerTimeoutMs &&
               m_process.state() == QProcess::Running) {
            m_process.waitForReadyRead(100);
        }
        if (!m_process.canReadLine())
            return {};
        return QJsonDocument::fromJson(m_process.readLine()).object();
    }

    QJsonObject request(const QString& method, const QJsonObject& params = {})
    {
        ++m_lastId;
        send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, m_lastId}, {u"method"_s, method}, {u"params"_s, params}});
        return next();
    }

    // Closes stdin, as a client that has gone does, and waits for the program to end: its exit code, or
    // -1 when it did not end.
    int close()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(kStopTimeoutMs))
            return -1;
        return m_process.exitStatus() == QProcess::NormalExit ? m_process.exitCode() : -1;
    }

    QString log() { return QString::fromUtf8(m_process.readAllStandardError()); }

private:
    QProcess m_process;
    int m_lastId = 0;
};

// mikser-studio --headless as the E2E instance, with its settings and cache in a temporary folder.
class HeadlessStudio {
public:
    HeadlessStudio()
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"MIKSERSTUDIO_INSTANCE"_s, u"e2e"_s);
        environment.insert(u"MIKSERSTUDIO_CONFIG_DIR"_s, m_folder.filePath(u"config"_s));
        environment.insert(u"MIKSERSTUDIO_CACHE_DIR"_s, m_folder.filePath(u"cache"_s));
        m_process.setProcessEnvironment(environment);
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.start(QStringLiteral(MIKSERSTUDIO_APP), {u"--headless"_s});
    }

    HeadlessStudio(const HeadlessStudio&) = delete;
    HeadlessStudio& operator=(const HeadlessStudio&) = delete;
    HeadlessStudio(HeadlessStudio&&) = delete;
    HeadlessStudio& operator=(HeadlessStudio&&) = delete;

    ~HeadlessStudio()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(kStopTimeoutMs);
        }
    }

    // Whether this run wrote its discovery file before it quit or took too long.
    bool waitForDiscoveryFile()
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kStartTimeoutMs && m_process.state() != QProcess::NotRunning) {
            QFile file(discoveryFilePath());
            if (file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(file.readAll())[u"pid"_s].toInteger() == m_process.processId()) {
                return true;
            }
            m_process.waitForFinished(100);
        }
        return false;
    }

    // Asks the app to close, as tests/e2e/HeadlessTests.cpp does: with taskkill without /F on Windows,
    // which reaches the headless app's hidden windows, and with SIGTERM elsewhere.
    bool stop()
    {
#ifdef Q_OS_WIN
        const QString taskkill = qEnvironmentVariable("SystemRoot") + u"/System32/taskkill.exe"_s;
        QProcess::execute(taskkill, {u"/PID"_s, QString::number(m_process.processId())});
#else
        m_process.terminate();
#endif
        return m_process.waitForFinished(kStopTimeoutMs);
    }

    QString output() { return QString::fromUtf8(m_process.readAll()); }

private:
    QTemporaryDir m_folder;
    QProcess m_process;
};

QJsonObject toolCall(int id, const QString& name, const QJsonObject& arguments = {})
{
    return QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                       {u"id"_s, id},
                       {u"method"_s, u"tools/call"_s},
                       {u"params"_s, QJsonObject{{u"name"_s, name}, {u"arguments"_s, arguments}}}};
}

bool isToolsChanged(const QJsonObject& message)
{
    return message[u"method"_s] == u"notifications/tools/list_changed"_s;
}

}  // namespace

TEST_CASE("The MCP bridge drives the headless studio and follows it as it starts and stops", "[e2e]")
{
    // A file a crashed run left behind would pass for this run's.
    QFile::remove(discoveryFilePath());

    McpClient client;
    const QJsonObject initialized = client.request(u"initialize"_s,
                                                   {{u"protocolVersion"_s, u"2025-11-25"_s},
                                                    {u"capabilities"_s, QJsonObject{{u"elicitation"_s, QJsonObject{}}}},
                                                    {u"clientInfo"_s, QJsonObject{{u"name"_s, u"e2e"_s}}}});
    INFO(client.log().toStdString());
    REQUIRE(initialized[u"result"_s][u"protocolVersion"_s] == u"2025-11-25"_s);
    client.send({{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}});
    CHECK(client.request(u"tools/list"_s)[u"result"_s][u"tools"_s].toArray().isEmpty());

    HeadlessStudio studio;
    REQUIRE(studio.waitForDiscoveryFile());
    INFO(studio.output().toStdString());
    CHECK(isToolsChanged(client.next()));

    QHash<QString, QJsonObject> tools;
    for (const QJsonValue tool : client.request(u"tools/list"_s)[u"result"_s][u"tools"_s].toArray())
        tools.insert(tool[u"name"_s].toString(), tool.toObject());
    REQUIRE(tools.contains(u"project_list"_s));
    CHECK(tools.value(u"project_list"_s)[u"annotations"_s][u"readOnlyHint"_s] == true);
    REQUIRE(tools.contains(u"project_new"_s));
    CHECK(tools.value(u"project_new"_s)[u"annotations"_s][u"destructiveHint"_s] == true);
    CHECK(tools.contains(u"recent_events"_s));

    const QJsonObject listed =
        client.request(u"tools/call"_s, {{u"name"_s, u"project_list"_s}})[u"result"_s].toObject();
    CHECK_FALSE(listed.contains(u"isError"_s));
    CHECK(listed[u"structuredContent"_s].isObject());

    client.send(toolCall(100, u"project_new"_s));
    const QJsonObject asked = client.next();
    REQUIRE(asked[u"method"_s] == u"elicitation/create"_s);
    client.send(
        {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, asked[u"id"_s]}, {u"result"_s, QJsonObject{{u"action"_s, u"accept"_s}}}});
    const QJsonObject created = client.next();
    CHECK(created[u"id"_s] == 100);
    CHECK_FALSE(created[u"result"_s].toObject().contains(u"isError"_s));

    // The event of the command arrives on the socket on its own time.
    bool seen = false;
    QElapsedTimer timer;
    timer.start();
    while (!seen && timer.elapsed() < kAnswerTimeoutMs) {
        const QJsonArray events = client
                                      .request(u"tools/call"_s,
                                               {{u"name"_s,
                                                 u"recent_events"_s}})[u"result"_s][u"structuredContent"_s][u"events"_s]
                                      .toArray();
        for (const QJsonValue event : events)
            seen = seen || (event[u"event"_s][u"type"_s] == u"command.executed"_s &&
                            event[u"event"_s][u"id"_s] == u"project.new"_s);
    }
    CHECK(seen);

    REQUIRE(studio.stop());
    CHECK(isToolsChanged(client.next()));
    CHECK(client.request(u"tools/list"_s)[u"result"_s][u"tools"_s].toArray().isEmpty());
    CHECK(client.close() == 0);
}
