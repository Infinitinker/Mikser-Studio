// The parts of Mikser Studio's local API the MCP bridge uses, faked for its tests: /v1/commands, running a
// command, the /v1/events socket and the discovery file.
#pragma once

#include <QHttpServer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QWebSocket>

#include <memory>

namespace mikserstudio::tests {

// The fake's session token.
inline constexpr char kFakeToken[] = "test-token";

// The commands the fake offers at first: edit.undo, which fails with nothing to undo;
// project.import_media, undoable, with a required path; project.list, read-only; and project.new, with a
// confirmation.
QJsonArray fakeCommands();

class FakeStudio {
public:
    // Listens on a free port and writes its discovery file to `discoveryPath`.
    explicit FakeStudio(QString discoveryPath);
    ~FakeStudio();

    FakeStudio(const FakeStudio&) = delete;
    FakeStudio& operator=(const FakeStudio&) = delete;
    FakeStudio(FakeStudio&&) = delete;
    FakeStudio& operator=(FakeStudio&&) = delete;

    void sendEvent(const QJsonObject& event);
    // What the app does when it stops: it removes its discovery file, closes its event sockets as going
    // away and stops listening.
    void stop();

    [[nodiscard]] qsizetype openSockets() const { return m_sockets.size(); }

    QJsonArray commands = fakeCommands();
    // Each command run: its id and params.
    QList<QPair<QString, QJsonObject>> runs;

private:
    QString m_discoveryPath;
    std::unique_ptr<QHttpServer> m_server;
    QList<QWebSocket*> m_sockets;
};

}  // namespace mikserstudio::tests
