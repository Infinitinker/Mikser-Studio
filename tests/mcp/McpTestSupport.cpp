// The MCP bridge tests' helpers: waiting, and the in-process client.
#include "McpTestSupport.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

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

Client::Client(const QString& discoveryPath, mcp::Bridge::Options options)
    : m_bridge(std::make_unique<mcp::Bridge>(
          discoveryPath, [this](const QJsonObject& message) { m_sent.append(message); }, options))
{
}

void Client::send(const QJsonObject& message)
{
    m_bridge->receive(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

QJsonObject Client::nextMessage()
{
    if (!waitFor([this] { return !m_sent.isEmpty(); }, std::chrono::seconds(5)))
        return {};
    return m_sent.takeFirst();
}

QJsonObject Client::request(const QString& method, const QJsonObject& params)
{
    ++m_lastId;
    send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, m_lastId}, {u"method"_s, method}, {u"params"_s, params}});
    return nextMessage();
}

void Client::start(const QJsonObject& capabilities, const QString& version)
{
    request(u"initialize"_s, QJsonObject{{u"protocolVersion"_s, version},
                                         {u"capabilities"_s, capabilities},
                                         {u"clientInfo"_s, QJsonObject{{u"name"_s, u"test"_s}}}});
    send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}});
}

QHash<QString, QJsonObject> Client::tools()
{
    QHash<QString, QJsonObject> byName;
    const QJsonArray listed = request(u"tools/list"_s)[u"result"_s][u"tools"_s].toArray();
    for (const QJsonValue tool : listed)
        byName.insert(tool[u"name"_s].toString(), tool.toObject());
    return byName;
}

QJsonObject Client::call(const QString& name, const QJsonObject& arguments)
{
    return request(u"tools/call"_s, QJsonObject{{u"name"_s, name}, {u"arguments"_s, arguments}})[u"result"_s]
        .toObject();
}

}  // namespace mikserstudio::tests
