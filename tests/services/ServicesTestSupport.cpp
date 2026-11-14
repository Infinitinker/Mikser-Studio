// Helpers the services tests share: waiting for the event loop, a media probe that needs no engine, and an
// in-process HTTP server that records requests.
#include "ServicesTestSupport.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QTcpSocket>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout)
{
    QDeadlineTimer deadline(timeout);
    while (!done()) {
        if (deadline.hasExpired())
            return done();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

void spin(std::chrono::milliseconds duration)
{
    QDeadlineTimer deadline(duration);
    while (!deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool portIsClosed(int port)
{
    QTcpSocket socket;
    socket.connectToHost(u"127.0.0.1"_s, static_cast<quint16>(port));
    return !socket.waitForConnected(1000);
}

std::expected<core::MediaInfo, QString> FakeProbe::probe(const QString& path, core::Rational frameRate)
{
    if (!QFileInfo::exists(path))
        return std::unexpected(u"missing"_s);
    const bool image = path.endsWith(u".png"_s);
    return core::MediaInfo{
        .kind = image ? core::MediaKind::Image : core::MediaKind::Video,
        .durationFrames = image ? 1 : 90,
        .width = 1920,
        .height = 1080,
        .frameRate = frameRate,
        .hasVideo = true,
        .hasAudio = !image,
        .audioSampleRate = image ? 0 : 48000,
        .audioChannels = image ? 0 : 2,
    };
}

namespace {

QByteArray methodName(QHttpServerRequest::Method method)
{
    switch (method) {
    case QHttpServerRequest::Method::Get:
        return "GET";
    case QHttpServerRequest::Method::Post:
        return "POST";
    case QHttpServerRequest::Method::Delete:
        return "DELETE";
    default:
        return "OTHER";
    }
}

}  // namespace

FakeHttpServer::FakeHttpServer()
    : m_server(std::make_unique<QHttpServer>())
{
    m_server->setMissingHandler(
        m_server.get(), [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
            RecordedRequest recorded{
                .method = methodName(request.method()),
                .path = request.url().path(QUrl::FullyEncoded),
                .authorization = request.headers().value(QHttpHeaders::WellKnownHeader::Authorization).toByteArray(),
                .body = request.body(),
            };
            m_requests.append(recorded);
            responder.sendResponse(m_handler ? m_handler(recorded)
                                             : QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound));
        });
    auto* tcp = new QTcpServer(m_server.get());
    REQUIRE(tcp->listen(QHostAddress::LocalHost, 0));
    REQUIRE(m_server->bind(tcp));
    m_port = tcp->serverPort();
}

FakeHttpServer::~FakeHttpServer() = default;

QUrl FakeHttpServer::url() const
{
    return QUrl(u"http://127.0.0.1:%1"_s.arg(m_port));
}

}  // namespace mikserstudio::tests
