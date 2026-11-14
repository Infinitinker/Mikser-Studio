// The Lua host's tests' fixture and helpers.
#include "LuaTestSupport.h"

#include "mikserstudio/core/PluginManager.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <mutex>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

namespace {

std::mutex logMutex;
QString logText;
QtMessageHandler previousHandler = nullptr;

void collect(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    {
        const std::scoped_lock lock(logMutex);
        logText += message + u'\n';
    }
    if (previousHandler && type >= QtWarningMsg)
        previousHandler(type, context, message);
}

}  // namespace

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.durationElapsed() > timeout)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

LogCapture::LogCapture()
{
    {
        const std::scoped_lock lock(logMutex);
        logText.clear();
    }
    previousHandler = qInstallMessageHandler(&collect);
}

LogCapture::~LogCapture()
{
    qInstallMessageHandler(previousHandler);
}

QString LogCapture::text() const
{
    const std::scoped_lock lock(logMutex);
    return logText;
}

LuaFixture::LuaFixture()
{
    options.bus = &bus;
    options.jobs = &jobs;
    options.snapshot = [this] { return project.snapshot(); };
    options.runLimit = std::chrono::milliseconds(500);
    options.memoryLimit = std::size_t{32} * 1024 * 1024;
}

LuaFixture::~LuaFixture()
{
    plugin.reset();
}

void LuaFixture::write(const QString& name, const QByteArray& contents) const
{
    const QString path = QDir(dir.path()).filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(contents);
}

bool LuaFixture::activate(const QByteArray& mainLua, const QStringList& granted)
{
    write(u"main.lua"_s, mainLua);
    if (!runtime)
        runtime = std::make_unique<lua::LuaRuntime>(options);
    core::PluginManifest manifest;
    manifest.id = u"test.plugin"_s;
    manifest.name = u"Test plugin"_s;
    manifest.version = u"1.0.0"_s;
    manifest.entry = u"main.lua"_s;
    manifest.directory = dir.path();
    const int before = starts;
    auto instance = runtime->activate(
        manifest, core::PluginActivation{.granted = granted,
                                         .settings = {{u"greeting"_s, u"Hi"_s}},
                                         .started = [this] { ++starts; },
                                         .failed = [this](const QString& message) { failure = message; }});
    if (!instance) {
        failure = instance.error();
        return false;
    }
    plugin = std::move(*instance);
    waitFor([this, before] { return starts > before || !failure.isEmpty(); });
    return starts > before;
}

std::expected<QJsonObject, core::CommandError> LuaFixture::run(const QString& id, const QJsonObject& params,
                                                               std::chrono::milliseconds timeout)
{
    std::optional<std::expected<QJsonObject, core::CommandError>> answer;
    bus.executeAsync(id, params, core::CallContext::api(), &bus,
                     [&answer](std::expected<QJsonObject, core::CommandError> result) { answer = std::move(result); });
    if (!waitFor([&answer] { return answer.has_value(); }, timeout))
        return std::unexpected(core::CommandError{core::CommandErrorCode::Failed, u"no answer in time"_s});
    return *answer;
}

}  // namespace mikserstudio::tests
