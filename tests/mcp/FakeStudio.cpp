// The fake Mikser Studio: its routes, its events socket and its discovery file.
#include "FakeStudio.h"

#include <QFile>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonDocument>
#include <QTcpServer>
#include <QWebSocketProtocol>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

namespace {

QJsonObject parse(const char* text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QHttpServerResponse error(QHttpServerResponse::StatusCode status, const QString& code, const QString& message)
{
    return QHttpServerResponse(QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}},
                               status);
}

QHttpServerResponse unauthorized()
{
    return error(QHttpServerResponse::StatusCode::Unauthorized, u"unauthorized"_s, u"No token."_s);
}

}  // namespace

QJsonArray fakeCommands()
{
    return QJsonArray{
        parse(R"({"id": "edit.undo", "title": "Undo", "description": "Reverts the last change.",
                  "paramsSchema": {"type": "object", "properties": {}, "additionalProperties": false},
                  "undoable": false, "readOnly": false, "confirmation": ""})"),
        parse(R"({"id": "project.import_media", "title": "Import media…",
                  "description": "Adds a media file to the media bin.",
                  "paramsSchema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
                  "undoable": true, "readOnly": false, "confirmation": ""})"),
        parse(R"({"id": "project.list", "title": "List project", "description": "Lists the media and the timeline.",
                  "paramsSchema": {}, "undoable": false, "readOnly": true, "confirmation": ""})"),
        parse(R"({"id": "project.new", "title": "New project", "description": "Starts an empty project.",
                  "paramsSchema": {}, "undoable": false, "readOnly": false,
                  "confirmation": "Start a new project? Unsaved changes will be lost."})"),
        parse(R"({"id": "project.save", "title": "Save project", "description": "Saves the project to a file.",
                  "paramsSchema": {"type": "object",
                                   "properties": {"path": {"type": "string"}, "overwrite": {"type": "boolean"}},
                                   "required": ["path", "overwrite"]},
                  "undoable": false, "readOnly": false, "confirmation": "", "confirmationParam": "overwrite"})"),
    };
}

FakeStudio::FakeStudio(QString discoveryPath, QByteArray token)
    : m_discoveryPath(std::move(discoveryPath))
    , m_token(std::move(token))
    , m_server(std::make_unique<QHttpServer>())
{
    m_server->route(u"/v1/commands"_s, QHttpServerRequest::Method::Get, [this](const QHttpServerRequest& request) {
        if (!isAuthorized(request))
            return unauthorized();
        if (listStatus != 200) {
            return error(static_cast<QHttpServerResponse::StatusCode>(listStatus), u"failed"_s,
                         u"The commands are not ready."_s);
        }
        return QHttpServerResponse(commands);
    });
    m_server->route(u"/v1/commands/<arg>"_s, QHttpServerRequest::Method::Post,
                    [this](const QString& id, const QHttpServerRequest& request) {
                        if (!isAuthorized(request))
                            return unauthorized();
                        runs.append({id, QJsonDocument::fromJson(request.body()).object()});
                        if (id == u"edit.undo"_s) {
                            return error(QHttpServerResponse::StatusCode::Conflict, u"not_available"_s,
                                         u"There is nothing to undo."_s);
                        }
                        if (id == u"project.save"_s && !runs.constLast().second.value(u"overwrite"_s).toBool()) {
                            return error(QHttpServerResponse::StatusCode::Conflict, u"needs_confirmation"_s,
                                         u"Replace the existing file?"_s);
                        }
                        return QHttpServerResponse(QJsonObject{{u"result"_s, QJsonObject{{u"ran"_s, id}}}});
                    });
    m_server->addWebSocketUpgradeVerifier(m_server.get(), [this](const QHttpServerRequest& request) {
        if (request.url().path() != u"/v1/events"_s || !isAuthorized(request))
            return QHttpServerWebSocketUpgradeResponse::deny();
        return QHttpServerWebSocketUpgradeResponse::accept();
    });
    QObject::connect(m_server.get(), &QHttpServer::newWebSocketConnection, m_server.get(), [this] {
        while (m_server->hasPendingWebSocketConnections()) {
            QWebSocket* socket = m_server->nextPendingWebSocketConnection().release();
            socket->setParent(m_server.get());
            m_sockets.append(socket);
            QObject::connect(socket, &QWebSocket::disconnected, socket, [this, socket] {
                m_sockets.removeOne(socket);
                socket->deleteLater();
            });
        }
    });

    auto* tcp = new QTcpServer(m_server.get());
    if (!tcp->listen(QHostAddress::LocalHost, 0) || !m_server->bind(tcp))
        qFatal("The fake Mikser Studio could not listen on 127.0.0.1.");
    const QString baseUrl = u"http://127.0.0.1:%1"_s.arg(tcp->serverPort());
    QFile file(m_discoveryPath);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("The fake Mikser Studio could not write its discovery file.");
    file.write(QJsonDocument(QJsonObject{
                                 {u"apiVersion"_s, 1},
                                 {u"baseUrl"_s, baseUrl},
                                 {u"eventsUrl"_s, u"ws://127.0.0.1:%1/v1/events"_s.arg(tcp->serverPort())},
                                 {u"pid"_s, 1},
                                 {u"port"_s, tcp->serverPort()},
                                 {u"token"_s, QString::fromLatin1(m_token)},
                             })
                   .toJson());
}

FakeStudio::~FakeStudio()
{
    stop();
}

bool FakeStudio::isAuthorized(const QHttpServerRequest& request) const
{
    return request.headers().value(QHttpHeaders::WellKnownHeader::Authorization) == "Bearer " + m_token;
}

void FakeStudio::sendEvent(const QJsonObject& event)
{
    const QString text = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
    for (QWebSocket* socket : std::as_const(m_sockets))
        socket->sendTextMessage(text);
}

void FakeStudio::stop()
{
    if (!m_server)
        return;
    QFile::remove(m_discoveryPath);
    const QList<QWebSocket*> sockets = std::exchange(m_sockets, {});
    for (QWebSocket* socket : sockets) {
        socket->disconnect();
        socket->close(QWebSocketProtocol::CloseCodeGoingAway);
    }
    m_server.reset();
}

}  // namespace mikserstudio::tests
