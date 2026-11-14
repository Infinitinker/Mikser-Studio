// Helpers the local API tests share: a fixture with a command bus and a server on a free port, and
// requests with or without the session token.
#pragma once

#include "HttpTestSupport.h"
#include "mikserstudio/api/ApiServer.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

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

// Runs the event loop until `done` returns true or the timeout passes; returns `done()`.
bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(10));

// A CommandBus with test commands and a started ApiServer whose discovery file is in a temp dir.
//   test.echo {text}           -> {text}
//   test.increment {by = 1}    -> {value}, undoable ("Increment Counter")
//   test.fail {}               -> Failed
//   test.busy {}               -> NotAvailable
//   test.noop {}               -> {}
//   test.refuse {code}         -> the error named by code: "permission_denied", "needs_confirmation"
//                                 or "canceled"
class ApiFixture {
public:
    ApiFixture();
    ~ApiFixture();

    [[nodiscard]] QUrl url(const QString& path) const;
    [[nodiscard]] QByteArray bearer() const;
    HttpReply get(const QString& path) const;
    HttpReply post(const QString& path, const QByteArray& body) const;

    QTemporaryDir dir;
    core::UndoStack undo;
    core::CommandBus bus{undo};
    int counter = 0;
    std::unique_ptr<api::ApiServer> server;
};

void registerTestCommands(core::CommandBus& bus, int& counter);

}  // namespace mikserstudio::tests
