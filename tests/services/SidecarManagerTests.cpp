// The sidecar manager: it starts the AI service, answers with its token and stops it cleanly, reports a
// missing environment, restarts a crashed or unhealthy service with backoff, and gives up or times out.
#include "ServicesTestSupport.h"
#include "mikserstudio/services/AiClient.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio::services;
using mikserstudio::tests::portIsClosed;
using mikserstudio::tests::waitFor;

namespace {

SidecarOptions fakeOptions(const QTemporaryDir& dir, const QStringList& extraArguments = {})
{
    SidecarOptions options;
    options.program = QStringLiteral(MIKSERSTUDIO_FAKE_SIDECAR);
    options.arguments = extraArguments;
    options.cacheDirectory = dir.filePath(u"cache"_s);
    options.readyTimeout = 10s;
    options.healthInterval = 60s;
    options.restartDelay = 50ms;
    options.maxRestarts = 2;
    options.stopTimeout = 3s;
    return options;
}

// Records the signals of a sidecar while it lives (the connections end with the log).
struct StateLog {
    QList<SidecarState> states;
    QStringList errors;
    QObject context;

    explicit StateLog(SidecarManager& sidecar)
    {
        QObject::connect(&sidecar, &SidecarManager::stateChanged, &context,
                         [this](SidecarState state) { states.append(state); });
        QObject::connect(&sidecar, &SidecarManager::errorOccurred, &context,
                         [this](const QString& message) { errors.append(message); });
    }
};

}  // namespace

TEST_CASE("The sidecar starts, answers with its token and stops cleanly", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarManager sidecar(fakeOptions(dir));
    StateLog log(sidecar);
    sidecar.start();
    CHECK(sidecar.state() == SidecarState::Starting);
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Running; }));
    CHECK(sidecar.baseUrl().host() == u"127.0.0.1"_s);
    const int port = sidecar.baseUrl().port();
    CHECK(port > 0);
    CHECK(sidecar.token().size() == 43);
    CHECK(sidecar.errorString().isEmpty());

    AiClient client;
    client.setEndpoint(sidecar.baseUrl(), sidecar.token());
    std::optional<std::expected<QList<AiBackendInfo>, QString>> backends;
    client.backends(&client, [&](auto answer) { backends = std::move(answer); });
    REQUIRE(waitFor([&] { return backends.has_value(); }));
    REQUIRE(*backends);
    CHECK(backends->value().at(0).id == u"fake"_s);

    QElapsedTimer timer;
    timer.start();
    sidecar.stop();
    CHECK(timer.elapsed() < 3000);  // It exits as soon as its stdin closes.
    CHECK(sidecar.state() == SidecarState::Stopped);
    CHECK(sidecar.baseUrl().isEmpty());
    CHECK(portIsClosed(port));
    CHECK(log.states == QList{SidecarState::Starting, SidecarState::Running, SidecarState::Stopped});
    CHECK(log.errors.isEmpty());

    sidecar.start();  // It can be started again.
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Running; }));
}

TEST_CASE("The sidecar runs in the cache folder and never runs programs from it by name", "[services][sidecar]")
{
    QTemporaryDir dir;
    const QString report = dir.filePath(u"launch.txt"_s);
    const SidecarOptions options = fakeOptions(dir, {u"--report-launch"_s, report});
    SidecarManager sidecar(options);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Running; }));

    QFile file(report);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
    REQUIRE(lines.size() >= 2);
    CHECK(QFileInfo(lines[0]).canonicalFilePath() == QFileInfo(options.cacheDirectory).canonicalFilePath());
    CHECK(lines[1] == u"1"_s);
}

TEST_CASE("A missing Python environment fails at once with the setup hint", "[services][sidecar]")
{
    QTemporaryDir dir;
    QFile pyproject(dir.filePath(u"pyproject.toml"_s));
    REQUIRE(pyproject.open(QIODevice::WriteOnly));
    pyproject.close();
    SidecarOptions options;
    options.cacheDirectory = dir.filePath(u"cache"_s);
    options.aiDirectory = dir.path();
    SidecarManager sidecar(options);
    StateLog log(sidecar);
    sidecar.start();
    CHECK(sidecar.state() == SidecarState::Failed);
    const QString expected =
        u"The AI service could not start: its Python environment \"%1\" was not found. Create it as docs/development.md describes and restart Mikser Studio."_s
            .arg(QDir::toNativeSeparators(dir.filePath(u".venv"_s)));
    CHECK(sidecar.errorString() == expected);
    CHECK(log.errors == QStringList{expected});
}

TEST_CASE("A program that cannot be run fails without restarts", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarOptions options = fakeOptions(dir);
    options.program = dir.filePath(u"no-such-program"_s);
    SidecarManager sidecar(options);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Failed; }));
    CHECK(sidecar.errorString().startsWith(u"The AI service could not start: \""_s));
    CHECK(sidecar.errorString().contains(u"could not be run"_s));
    CHECK(sidecar.restartCount() == 0);
}

TEST_CASE("A sidecar that keeps exiting is restarted with backoff, then given up", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarManager sidecar(fakeOptions(dir, {u"--mode"_s, u"exit"_s}));
    StateLog log(sidecar);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Failed; }));
    CHECK(sidecar.restartCount() == 2);
    CHECK(log.states.count(SidecarState::Restarting) == 2);
    CHECK(log.states.count(SidecarState::Starting) == 3);
    REQUIRE(log.errors.size() == 3);
    CHECK(
        log.errors[0] ==
        u"The AI service exited before it was ready (exit code 3): fake sidecar: refusing to start. Restarting it (attempt 1 of 2)."_s);
    CHECK(sidecar.errorString().startsWith(
        u"The AI service exited before it was ready (exit code 3): fake sidecar: refusing to start. Mikser Studio gave up after 2 restarts"_s));

    sidecar.start();  // From Failed, start() tries again with a full budget.
    CHECK(sidecar.state() == SidecarState::Starting);
    CHECK(sidecar.restartCount() == 0);
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Failed; }));
}

TEST_CASE("A sidecar that never gets ready times out", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarOptions options = fakeOptions(dir, {u"--mode"_s, u"silent"_s});
    options.readyTimeout = 500ms;
    options.maxRestarts = 0;
    SidecarManager sidecar(options);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Failed; }));
    CHECK(sidecar.errorString().startsWith(u"The AI service did not report that it was ready within 0.5 seconds."_s));
}

TEST_CASE("A crash after start-up is followed by a restart", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarManager sidecar(fakeOptions(dir, {u"--mode"_s, u"crash-after-ready"_s, u"--crash-times"_s, u"1"_s,
                                             u"--counter"_s, dir.filePath(u"count"_s)}));
    StateLog log(sidecar);
    sidecar.start();
    REQUIRE(waitFor([&] { return log.states.count(SidecarState::Running) == 2; }));
    CHECK(sidecar.state() == SidecarState::Running);
    CHECK(sidecar.restartCount() == 1);
    CHECK(log.states == QList{SidecarState::Starting, SidecarState::Running, SidecarState::Restarting,
                              SidecarState::Starting, SidecarState::Running});
    REQUIRE(log.errors.size() == 1);
    CHECK(
        log.errors[0] ==
        u"The AI service stopped unexpectedly (exit code 1): fake sidecar: crashing on purpose. Restarting it (attempt 1 of 2)."_s);
    CHECK(sidecar.errorString().isEmpty());  // Cleared once it runs again.
}

TEST_CASE("A sidecar that fails its health checks is restarted", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarOptions options = fakeOptions(dir, {u"--mode"_s, u"unhealthy"_s});
    options.healthInterval = 100ms;
    options.healthFailuresBeforeRestart = 2;
    options.maxRestarts = 0;
    SidecarManager sidecar(options);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Running; }));
    const int port = sidecar.baseUrl().port();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Failed; }));
    CHECK(sidecar.errorString().startsWith(u"The AI service stopped answering health checks."_s));
    CHECK(portIsClosed(port));
}

TEST_CASE("A sidecar that ignores its closed stdin is terminated or killed", "[services][sidecar]")
{
    QTemporaryDir dir;
    SidecarOptions options = fakeOptions(dir, {u"--mode"_s, u"ignore-stdin"_s});
    options.stopTimeout = 300ms;
    SidecarManager sidecar(options);
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() == SidecarState::Running; }));
    const int port = sidecar.baseUrl().port();
    QElapsedTimer timer;
    timer.start();
    sidecar.stop();
    CHECK(timer.elapsed() < 4000);
    CHECK(sidecar.state() == SidecarState::Stopped);
    CHECK(portIsClosed(port));
}

TEST_CASE("Destroying the manager stops the sidecar", "[services][sidecar]")
{
    QTemporaryDir dir;
    auto sidecar = std::make_unique<SidecarManager>(fakeOptions(dir));
    sidecar->start();
    REQUIRE(waitFor([&] { return sidecar->state() == SidecarState::Running; }));
    const int port = sidecar->baseUrl().port();
    sidecar.reset();
    CHECK(portIsClosed(port));
}

TEST_CASE("State names", "[services][sidecar]")
{
    CHECK(toString(SidecarState::Stopped) == u"stopped"_s);
    CHECK(toString(SidecarState::Starting) == u"starting"_s);
    CHECK(toString(SidecarState::Running) == u"running"_s);
    CHECK(toString(SidecarState::Restarting) == u"restarting"_s);
    CHECK(toString(SidecarState::Failed) == u"failed"_s);
}
