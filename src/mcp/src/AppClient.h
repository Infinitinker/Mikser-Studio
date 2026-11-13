// The running Mikser Studio, as its local API shows it: the commands it offers, and running them.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>

#include <expected>
#include <functional>
#include <variant>

class QObject;

namespace mikserstudio::mcp {

// What to tell the client while the app cannot be reached.
QString notRunningMessage();

// A command the app refused or that failed: the API's error code and message.
struct CommandError {
    QString code;
    QString message;
};

// Talks to the Mikser Studio whose discovery file is at `discoveryPath`, reading the file anew for each
// request, since the app writes a new port and token at each start. Each request belongs to a `context`:
// deleting it aborts the request, and `done` is then never called. Otherwise `done` is called once, with
// what to tell the client when the app could not be reached.
class AppClient {
public:
    using Commands = std::expected<QJsonArray, QString>;
    using Outcome = std::expected<std::variant<QJsonObject, CommandError>, QString>;

    explicit AppClient(QString discoveryPath);

    // Every command the app offers now, as GET /v1/commands lists them.
    void commands(QObject* context, std::function<void(Commands)> done);

    // Runs a command: its result, or the error the app answered with.
    void run(QObject* context, const QString& commandId, const QJsonObject& params, std::function<void(Outcome)> done);

    [[nodiscard]] const QString& discoveryPath() const { return m_discoveryPath; }

private:
    QString m_discoveryPath;
    QNetworkAccessManager m_network;
};

}  // namespace mikserstudio::mcp
