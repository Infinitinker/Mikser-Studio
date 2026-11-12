// The local API tests' helpers: the test commands, the fixture that runs a server on a free port, and
// HTTP requests with or without the session token.
#include "ApiTestSupport.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

namespace {

constexpr auto kRequestTimeout = std::chrono::seconds(10);

QJsonObject parse(const char* text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

class IncrementEdit final : public core::UndoableEdit {
public:
    IncrementEdit(int& counter, int by)
        : m_counter(counter)
        , m_by(by)
    {
    }
    void undo() override { m_counter -= m_by; }
    void redo() override { m_counter += m_by; }
    [[nodiscard]] QString text() const override { return u"Increment Counter"_s; }

private:
    int& m_counter;
    int m_by;
};

core::CommandSpec spec(const QString& id, const char* schema, bool undoable = false)
{
    return core::CommandSpec{
        .id = id,
        .title = u"Test "_s + id,
        .description = u"A command used by the API tests."_s,
        .category = u"Test"_s,
        .paramsSchema = parse(schema),
        .shortcut = {},
        .undoable = undoable,
    };
}

}  // namespace

bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout)
{
    const QDeadlineTimer deadline(timeout);
    while (!done()) {
        if (deadline.hasExpired())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

QString HttpReply::errorCode() const
{
    return object().value(u"error"_s).toObject().value(u"code"_s).toString();
}

HttpReply sendRequest(const QByteArray& method, const QUrl& url, const QByteArray& body,
                      const std::optional<QByteArray>& authorization)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeout);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    if (authorization)
        request.setRawHeader("Authorization", *authorization);

    QNetworkReply* reply = manager.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kRequestTimeout + std::chrono::seconds(1), &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();

    HttpReply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.headers = reply->headers();
    if (reply->error() != QNetworkReply::NoError)
        result.networkError = reply->errorString();
    delete reply;
    return result;
}

void registerTestCommands(core::CommandBus& bus, int& counter)
{
    REQUIRE(
        bus.registerCommand(spec(u"test.echo"_s,
                                 R"({"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"],
                 "additionalProperties": false})"),
                            [](const QJsonObject& params) -> core::CommandResult {
                                return core::CommandOutcome{QJsonObject{{u"text"_s, params.value(u"text"_s)}}, nullptr};
                            }));
    REQUIRE(bus.registerCommand(
        spec(u"test.increment"_s,
             R"({"type": "object", "properties": {"by": {"type": "integer", "minimum": 1, "default": 1}},
                 "additionalProperties": false})",
             true),
        [&counter](const QJsonObject& params) -> core::CommandResult {
            const int by = params.value(u"by"_s).toInt();
            counter += by;
            return core::CommandOutcome{QJsonObject{{u"value"_s, counter}},
                                        std::make_unique<IncrementEdit>(counter, by)};
        }));
    REQUIRE(bus.registerCommand(spec(u"test.fail"_s, "{}"), [](const QJsonObject&) -> core::CommandResult {
        return core::commandFailure(core::CommandErrorCode::Failed, u"The test command failed on purpose."_s);
    }));
    REQUIRE(bus.registerCommand(spec(u"test.busy"_s, "{}"), [](const QJsonObject&) -> core::CommandResult {
        return core::commandFailure(core::CommandErrorCode::NotAvailable, u"The test command is busy."_s);
    }));
    REQUIRE(bus.registerCommand(spec(u"test.noop"_s, "{}"),
                                [](const QJsonObject&) -> core::CommandResult { return core::CommandOutcome{}; }));
}

ApiFixture::ApiFixture()
{
    REQUIRE(dir.isValid());
    registerTestCommands(bus, counter);
    server = std::make_unique<api::ApiServer>(
        bus, api::ApiServerOptions{.port = 0, .token = {}, .discoveryFile = dir.filePath(u"api.json"_s)});
    const auto started = server->start();
    if (!started)
        FAIL("ApiServer::start failed: " << started.error().toStdString());
    REQUIRE(*started != 0);
}

ApiFixture::~ApiFixture()
{
    server.reset();
}

QUrl ApiFixture::url(const QString& path) const
{
    return QUrl(server->baseUrl() + path);
}

QByteArray ApiFixture::bearer() const
{
    return "Bearer " + server->token().toUtf8();
}

HttpReply ApiFixture::get(const QString& path) const
{
    return sendRequest("GET", url(path), {}, bearer());
}

HttpReply ApiFixture::post(const QString& path, const QByteArray& body) const
{
    return sendRequest("POST", url(path), body, bearer());
}

}  // namespace mikserstudio::tests
