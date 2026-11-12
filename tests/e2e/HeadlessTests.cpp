// End to end: the built mikser-studio, run headless as the E2E instance and driven from outside, the
// way a user's tool drives it. It publishes its discovery file, answers the local API with the token
// from that file, and, asked to close, shuts down cleanly and removes the file.
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

Reply get(const QString& url, const QString& token = {})
{
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kRequestTimeoutMs);
    if (!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    QNetworkReply* reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Reply result{.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                 .body = QJsonDocument::fromJson(reply->readAll())};
    reply->deleteLater();
    return result;
}

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

    const Reply health = get(baseUrl + u"/v1/health"_s);
    CHECK(health.status == 200);
    CHECK(health.body.object().value(u"apiVersion"_s).toInt() == 1);

    CHECK(get(baseUrl + u"/v1/commands"_s).status == 401);

    const Reply commands = get(baseUrl + u"/v1/commands"_s, token);
    REQUIRE(commands.status == 200);
    const QJsonArray list = commands.body.array();
    CHECK(std::ranges::any_of(list, [](const QJsonValue& command) {
        return command.toObject().value(u"id"_s).toString() == u"edit.undo"_s;
    }));

    REQUIRE(studio.stop());
    INFO(studio.output().toStdString());
    CHECK(studio.exitStatus() == QProcess::NormalExit);
    CHECK(studio.exitCode() == 0);
    CHECK_FALSE(QFile::exists(discoveryFilePath()));
}
