// End to end: the built mikser-studio, run headless as the E2E instance and driven from outside, the
// way a user's tool drives it. It publishes its discovery file, answers the local API with the token
// from that file, saves a project, and, asked to close, shuts down cleanly and removes the file.
#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace {

constexpr int kStartTimeoutMs = 60'000;
constexpr int kStopTimeoutMs = 30'000;
constexpr int kRequestTimeoutMs = 10'000;

// Where the E2E instance writes its discovery file: main.cpp names the tests like that instance.
QString discoveryFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/api.json"_s;
}

QJsonObject readDiscoveryFile()
{
    QFile file(discoveryFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

struct Reply {
    int status = 0;
    QJsonDocument body;
};

// A GET, or a POST of `body` when there is one.
Reply send(const QString& url, const QString& token = {}, const QJsonObject& body = {})
{
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kRequestTimeoutMs);
    if (!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    QNetworkReply* reply = nullptr;
    if (body.isEmpty()) {
        reply = network.get(request);
    } else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Reply result{.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                 .body = QJsonDocument::fromJson(reply->readAll())};
    reply->deleteLater();
    return result;
}

#ifdef Q_OS_WIN
struct WindowSearch {
    DWORD pid = 0;
    QStringList found;
};

void addIfOwned(HWND window, WindowSearch& search, QStringView kind)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner != search.pid)
        return;
    wchar_t name[128] = {};
    GetClassNameW(window, name, 128);
    search.found.append(u"%1 window of class %2"_s.arg(kind, QString::fromWCharArray(name)));
}

// The windows of process `pid`, as `taskkill` chooses among them: top-level ones first, then
// message-only ones. Says where a close request went when the app did not close.
QStringList windowsOf(qint64 pid)
{
    WindowSearch search{.pid = static_cast<DWORD>(pid), .found = {}};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            addIfOwned(window, *reinterpret_cast<WindowSearch*>(data), u"top-level");
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr)))
        addIfOwned(window, search, u"message-only");
    return search.found;
}
#endif

// mikser-studio --headless as the E2E instance, with its settings and cache in a temporary folder.
class HeadlessStudio {
public:
    HeadlessStudio()
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"MIKSERSTUDIO_INSTANCE"_s, u"e2e"_s);
        environment.insert(u"MIKSERSTUDIO_CONFIG_DIR"_s, m_folder.filePath(u"config"_s));
        environment.insert(u"MIKSERSTUDIO_CACHE_DIR"_s, m_folder.filePath(u"cache"_s));
        environment.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
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

    // The discovery file this run wrote, once it has: empty if the app quit or took too long.
    QJsonObject waitForDiscoveryFile()
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kStartTimeoutMs && m_process.state() != QProcess::NotRunning) {
            if (const QJsonObject discovery = readDiscoveryFile();
                discovery.value(u"pid"_s).toInteger() == m_process.processId()) {
                return discovery;
            }
            m_process.waitForFinished(100);
        }
        return {};
    }

    // Asks the app to close, as a user's tool would: with `taskkill` without /F on Windows, with SIGTERM
    // elsewhere. QProcess::terminate() on Windows reaches only top-level windows, which the headless
    // app has none of; taskkill also reaches its hidden ones (src/app/ShutdownSignals.cpp).
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

    // The app's windows, where a close request from `taskkill` can go.
    [[nodiscard]] QString windows() const
    {
#ifdef Q_OS_WIN
        return windowsOf(m_process.processId()).join(u", "_s);
#else
        return {};
#endif
    }

    // A folder of this run's own, removed with it.
    [[nodiscard]] const QTemporaryDir& folder() const { return m_folder; }

    [[nodiscard]] int exitCode() const { return m_process.exitCode(); }
    [[nodiscard]] QProcess::ExitStatus exitStatus() const { return m_process.exitStatus(); }
    QString output() { return QString::fromUtf8(m_process.readAll()); }

private:
    QTemporaryDir m_folder;
    QProcess m_process;
};

}  // namespace

TEST_CASE("The headless studio answers its API and removes its discovery file when it closes", "[e2e]")
{
    // A file a crashed run left behind would pass for this run's.
    QFile::remove(discoveryFilePath());

    HeadlessStudio studio;
    const QJsonObject discovery = studio.waitForDiscoveryFile();
    INFO(studio.output().toStdString());
    REQUIRE_FALSE(discovery.isEmpty());
    const QString baseUrl = discovery.value(u"baseUrl"_s).toString();
    const QString token = discovery.value(u"token"_s).toString();
    CHECK(discovery.value(u"apiVersion"_s).toInt() == 1);

    const Reply health = send(baseUrl + u"/v1/health"_s);
    CHECK(health.status == 200);
    CHECK(health.body.object().value(u"apiVersion"_s).toInt() == 1);

    CHECK(send(baseUrl + u"/v1/commands"_s).status == 401);

    const Reply commands = send(baseUrl + u"/v1/commands"_s, token);
    REQUIRE(commands.status == 200);
    const QJsonArray list = commands.body.array();
    CHECK(std::ranges::any_of(list, [](const QJsonValue& command) {
        return command.toObject().value(u"id"_s).toString() == u"edit.undo"_s;
    }));

    // A project saved to a path without an extension gets the project extension.
    const QString saveTo = studio.folder().filePath(u"first"_s);
    const Reply saved = send(baseUrl + u"/v1/commands/project.save"_s, token, {{u"path"_s, saveTo}});
    CHECK(saved.status == 200);
    CHECK(saved.body.object().value(u"result"_s).toObject().value(u"path"_s).toString() == saveTo + u".msproj"_s);
    CHECK(QFile::exists(saveTo + u".msproj"_s));

    // The project's own file is saved again without asking, also without a path.
    CHECK(send(baseUrl + u"/v1/commands/project.save"_s, token, {{u"path"_s, saveTo}}).status == 200);
    CHECK(send(baseUrl + u"/v1/commands/project.save"_s, token, {{u"overwrite"_s, false}}).status == 200);

    // Saving over another file needs the person's agreement, given with overwrite.
    const QString other = studio.folder().filePath(u"second"_s);
    REQUIRE(send(baseUrl + u"/v1/commands/project.save_as"_s, token, {{u"path"_s, other}}).status == 200);
    const Reply asked = send(baseUrl + u"/v1/commands/project.save"_s, token, {{u"path"_s, saveTo}});
    CHECK(asked.status == 409);
    CHECK(asked.body.object().value(u"error"_s).toObject().value(u"code"_s).toString() == u"needs_confirmation"_s);
    const Reply replaced = send(baseUrl + u"/v1/commands/project.save"_s, token,
                                {{u"path"_s, saveTo}, {u"overwrite"_s, true}});
    CHECK(replaced.status == 200);

    const QString windows = studio.windows();
    const bool stopped = studio.stop();
    INFO("Its windows when asked to close: " << windows.toStdString());
    INFO(studio.output().toStdString());
    REQUIRE(stopped);
    CHECK(studio.exitStatus() == QProcess::NormalExit);
    CHECK(studio.exitCode() == 0);
    CHECK_FALSE(QFile::exists(discoveryFilePath()));
}
