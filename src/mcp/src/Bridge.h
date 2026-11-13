// The MCP side of the bridge: answers a client's JSON-RPC messages with the app's commands as tools.
#pragma once

#include "AppClient.h"
#include "EventFollower.h"
#include "RecentEvents.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <functional>

namespace mikserstudio::mcp {

// The protocol versions the bridge speaks, newest first. Structured results came with 2025-06-18.
QStringList protocolVersions();

// JSON-RPC's error codes.
enum RpcError : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
};

// One client's session. receive() takes each message from the client; `send` writes one to it.
class Bridge : public QObject {
    Q_OBJECT

public:
    using Send = std::function<void(const QJsonObject&)>;

    struct Options {
        // How long to wait before looking for the app again once it is gone.
        std::chrono::milliseconds retry{std::chrono::seconds(2)};
        // How long requests still running may take to answer once the client has closed its input.
        std::chrono::milliseconds closeTimeout{std::chrono::seconds(10)};
    };

    Bridge(QString discoveryPath, Send send, Options options, QObject* parent = nullptr);

    // Starts following the app's events.
    void start();

    void receive(const QByteArray& line);

    // Ends the session once the client has gone: questions to the person count as unanswered, and
    // requests still running get a little time to send their answers. Then closed() is emitted.
    void close();

Q_SIGNALS:
    void closed();

private:
    // A question to the person that waits for the client's answer.
    struct Question {
        QPointer<QObject> request;
        std::function<void(bool agreed)> answered;
    };

    void notified(const QString& method, const QJsonObject& params);
    void answered(const QJsonObject& message);
    void request(const QJsonValue& id, const QString& method, const QJsonObject& params);
    QJsonObject initialize(const QJsonObject& params);
    void listTools(const QJsonValue& id, QObject* context);
    void callTool(const QJsonValue& id, const QJsonObject& params, QObject* context);
    void runCommand(const QJsonValue& id, const QJsonObject& command, const QJsonObject& arguments, QObject* context);
    void ask(QObject* context, const QString& confirmation, std::function<void(bool agreed)> answered);

    // A tool's result: the command's result as JSON text and, for clients of 2025-06-18 or later, as
    // structured content.
    [[nodiscard]] QJsonObject success(const QJsonObject& result) const;
    QObject* startRequest(const QJsonValue& id);
    void finishRequest(const QJsonValue& id, const QJsonObject& result);
    void failRequest(const QJsonValue& id, int code, const QString& message);
    void endRequest(const QJsonValue& id);
    void toolsChanged();
    void closeWhenIdle();

    AppClient m_app;
    Send m_send;
    Options m_options;
    EventFollower m_events;
    RecentEvents m_recent;
    QString m_protocol;
    bool m_canAsk = false;  // Whether the client can ask the person (elicitation).
    bool m_ready = false;  // Notifications go out only after the client's notifications/initialized.
    bool m_closing = false;
    bool m_closed = false;
    int m_questionCount = 0;
    QHash<QString, Question> m_questions;
    QHash<QString, QObject*> m_running;  // Each running request's context, by its id.
    QTimer m_closeTimer;
};

}  // namespace mikserstudio::mcp
