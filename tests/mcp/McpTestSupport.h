// Helpers the MCP bridge's tests share: waiting on the event loop, and an MCP client of a Bridge in the
// same process.
#pragma once

#include "Bridge.h"

#include <catch2/catch_tostring.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

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

// An MCP client talking to a Bridge: requests and their answers, and what the bridge sends it.
class Client {
public:
    explicit Client(const QString& discoveryPath, mcp::Bridge::Options options = {});

    void send(const QJsonObject& message);
    // The next message the bridge sends, or an empty object when none comes within five seconds.
    QJsonObject nextMessage();
    // Sends a request and returns the bridge's answer to it.
    QJsonObject request(const QString& method, const QJsonObject& params = {});
    // initialize, then notifications/initialized.
    void start(const QJsonObject& capabilities = {}, const QString& version = QStringLiteral("2025-06-18"));
    // tools/list's tools, by name.
    QHash<QString, QJsonObject> tools();
    // tools/call's result.
    QJsonObject call(const QString& name, const QJsonObject& arguments = {});

    mcp::Bridge& bridge() { return *m_bridge; }

private:
    QList<QJsonObject> m_sent;
    std::unique_ptr<mcp::Bridge> m_bridge;
    int m_lastId = 0;
};

}  // namespace mikserstudio::tests
