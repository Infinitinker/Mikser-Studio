// The Lua host's tests' fixture: a command bus, job manager and project, and one Lua plugin
// written to a temporary folder; and Catch2 printers for Qt types.
#pragma once

#include "mikserstudio/core/Command.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/lua/LuaRuntime.h"

#include <catch2/catch_tostring.hpp>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <string>

template <> struct Catch::StringMaker<QString> {
    static std::string convert(const QString& value) { return '"' + value.toStdString() + '"'; }
};

template <> struct Catch::StringMaker<QJsonObject> {
    static std::string convert(const QJsonObject& value)
    {
        return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
    }
};

namespace mikserstudio::tests {

// Runs the event loop until `done` or the timeout; true when `done`.
bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(10));

// Collects what the app logs while it lives.
class LogCapture {
public:
    LogCapture();
    ~LogCapture();
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] QString text() const;
};

struct LuaFixture {
    LuaFixture();
    ~LuaFixture();
    LuaFixture(const LuaFixture&) = delete;
    LuaFixture& operator=(const LuaFixture&) = delete;

    // Writes a file into the plugin's folder.
    void write(const QString& name, const QByteArray& contents) const;
    // Writes main.lua and activates the plugin "test.plugin" with the permissions it is granted;
    // true when it started.
    bool activate(const QByteArray& mainLua, const QStringList& granted = core::allPermissions());
    // Runs a command through the bus, as the API would, and waits for the answer.
    std::expected<QJsonObject, core::CommandError> run(const QString& id, const QJsonObject& params = {},
                                                       std::chrono::milliseconds timeout = std::chrono::seconds(10));

    int argc = 0;
    QCoreApplication app{argc, nullptr};
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::JobManager jobs{bus};
    core::ProjectModel project;
    QTemporaryDir dir;
    lua::LuaRuntimeOptions options;
    std::unique_ptr<lua::LuaRuntime> runtime;
    std::unique_ptr<core::PluginInstance> plugin;
    QStringList network;  // The test plugin's network entries.
    int starts = 0;
    QString failure;
};

}  // namespace mikserstudio::tests
