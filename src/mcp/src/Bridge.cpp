// The bridge's MCP methods: initialize, ping, tools/list and tools/call, with questions to the person
// through elicitation and notifications when the tools change.
#include "Bridge.h"

#include "Logging.h"
#include "ToolList.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::mcp {

namespace {

const QString kStructuredSince = u"2025-06-18"_s;

const QString kInstructions =
    u"These tools run Mikser Studio's commands in the project the person has open, the same commands its "
    "menus run. Read the project with project_list before changing it, and see what the person changed "
    "since with recent_events. Every change shows in the app, and undoable ones can be taken back with "
    "edit_undo. AI commands start a job and return its id: follow it with ai_jobs. When no tools are listed, "
    "Mikser Studio is not running; the list fills in once it starts."_s;

// A request's id as a key: its JSON, so 1 and "1" stay apart.
QString keyOf(const QJsonValue& id)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact));
}

QJsonObject failure(const QString& message)
{
    return QJsonObject{{u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, message}}}},
                       {u"isError"_s, true}};
}

}  // namespace

QStringList protocolVersions()
{
    return {u"2025-11-25"_s, u"2025-06-18"_s, u"2025-03-26"_s, u"2024-11-05"_s};
}

Bridge::Bridge(QString discoveryPath, Send send, Options options, QObject* parent)
    : QObject(parent)
    , m_app(discoveryPath)
    , m_send(std::move(send))
    , m_options(options)
    , m_events(std::move(discoveryPath), options.retry, options.handshakeTimeout)
    , m_protocol(protocolVersions().constFirst())
{
    m_closeTimer.setSingleShot(true);
    connect(&m_closeTimer, &QTimer::timeout, this, [this] {
        for (QObject* context : std::as_const(m_running))
            delete context;
        m_running.clear();
        closeWhenIdle();
    });
    connect(&m_events, &EventFollower::connected, this, [this] {
        qCInfo(lcMcp) << "Connected to Mikser Studio.";
        toolsChanged();
    });
    connect(&m_events, &EventFollower::disconnected, this, [this] {
        qCInfo(lcMcp) << "Mikser Studio stopped.";
        toolsChanged();
    });
    connect(&m_events, &EventFollower::eventReceived, this, [this](const QJsonObject& event) {
        m_recent.add(event, QDateTime::currentDateTimeUtc());
        const QString type = event.value(u"type"_s).toString();
        if (type == u"command.registered"_s || type == u"command.unregistered"_s)
            toolsChanged();
    });
}

void Bridge::start()
{
    m_events.start();
}

void Bridge::receive(const QByteArray& line)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError) {
        failRequest(QJsonValue::Null, ParseError, u"The message is not JSON."_s);
        return;
    }
    const QJsonObject message = document.object();
    if (!document.isObject() || message.value(u"jsonrpc"_s) != u"2.0"_s) {
        failRequest(QJsonValue::Null, InvalidRequest, u"The message is not a JSON-RPC 2.0 object."_s);
        return;
    }
    const QString method = message.value(u"method"_s).toString();
    const QJsonObject params = message.value(u"params"_s).toObject();
    if (!message.contains(u"method"_s))
        answered(message);
    else if (message.contains(u"id"_s))
        request(message.value(u"id"_s), method, params);
    else
        notified(method, params);
}

void Bridge::close()
{
    m_closing = true;
    const QHash<QString, Question> questions = std::exchange(m_questions, {});
    for (const Question& question : questions) {
        if (question.request)
            question.answered(false);
    }
    if (!m_running.isEmpty())
        m_closeTimer.start(m_options.closeTimeout);
    closeWhenIdle();
}

void Bridge::notified(const QString& method, const QJsonObject& params)
{
    if (method == u"notifications/initialized"_s) {
        m_ready = true;
    } else if (method == u"notifications/cancelled"_s) {
        // The client expects no answer to a request it canceled.
        delete m_running.take(keyOf(params.value(u"requestId"_s)));
        closeWhenIdle();
    }
}

void Bridge::answered(const QJsonObject& message)
{
    const Question question = m_questions.take(message.value(u"id"_s).toString());
    if (!question.request)
        return;
    const QJsonValue result = message.value(u"result"_s);
    question.answered(result.isObject() && result[u"action"_s] == u"accept"_s);
}

void Bridge::request(const QJsonValue& id, const QString& method, const QJsonObject& params)
{
    if (method == u"initialize"_s) {
        finishRequest(id, initialize(params));
    } else if (method == u"ping"_s) {
        finishRequest(id, {});
    } else if (method == u"tools/list"_s) {
        listTools(id, startRequest(id));
    } else if (method == u"tools/call"_s) {
        callTool(id, params, startRequest(id));
    } else {
        failRequest(id, MethodNotFound, u"This bridge has no method “%1”."_s.arg(method));
    }
}

QJsonObject Bridge::initialize(const QJsonObject& params)
{
    const QString askedFor = params.value(u"protocolVersion"_s).toString();
    m_protocol = protocolVersions().contains(askedFor) ? askedFor : protocolVersions().constFirst();
    m_canAsk = params.value(u"capabilities"_s)[u"elicitation"_s].isObject();
    return QJsonObject{
        {u"protocolVersion"_s, m_protocol},
        {u"capabilities"_s, QJsonObject{{u"tools"_s, QJsonObject{{u"listChanged"_s, true}}}}},
        {u"serverInfo"_s, QJsonObject{{u"name"_s, u"mikser-studio"_s},
                                      {u"title"_s, u"Mikser Studio"_s},
                                      {u"version"_s, QStringLiteral(MIKSERSTUDIO_VERSION_STRING)}}},
        {u"instructions"_s, kInstructions},
    };
}

void Bridge::listTools(const QJsonValue& id, QObject* context)
{
    m_app.commands(context, [this, id](const AppClient::Commands& commands) {
        // While the app is not running there are no tools, not even the bridge's own.
        QJsonArray tools;
        if (commands) {
            tools = toolsFor(*commands, {RecentEvents::toolName()}, results()).tools;
            tools.append(RecentEvents::tool());
        }
        finishRequest(id, QJsonObject{{u"tools"_s, tools}});
    });
}

void Bridge::callTool(const QJsonValue& id, const QJsonObject& params, QObject* context)
{
    const QJsonValue name = params.value(u"name"_s);
    const QJsonValue given = params.value(u"arguments"_s);
    if (!name.isString() || !(given.isObject() || given.isNull() || given.isUndefined())) {
        failRequest(id, InvalidParams, u"tools/call needs a tool name and an object of arguments."_s);
        return;
    }
    const QJsonObject arguments = given.toObject();
    if (name == RecentEvents::toolName()) {
        if (const std::optional<QJsonObject> result = m_recent.result(arguments))
            finishRequest(id, success(*result));
        else
            failRequest(id, InvalidParams, u"recent_events takes after, a whole number from 0 to 2^53."_s);
        return;
    }
    m_app.commands(
        context, [this, id, name = name.toString(), arguments, context](const AppClient::Commands& commands) {
            if (!commands) {
                finishRequest(id, failure(commands.error()));
                return;
            }
            const QJsonObject command = toolsFor(*commands, {RecentEvents::toolName()}, results()).commands.value(name);
            if (command.isEmpty()) {
                failRequest(id, InvalidParams, u"Mikser Studio has no tool “%1”. List the tools again."_s.arg(name));
                return;
            }
            const QString confirmation = command.value(u"confirmation"_s).toString();
            if (confirmation.isEmpty() || command.value(u"readOnly"_s).toBool()) {
                runCommand(id, command, arguments, context);
                return;
            }
            if (!m_canAsk) {
                finishRequest(id, failure(u"This command needs the person's agreement (“%1”), and this MCP client "
                                          "cannot ask for it. Ask the person to run it in Mikser Studio instead."_s.arg(
                                              confirmation)));
                return;
            }
            ask(context, confirmation, [this, id, command, arguments, context](bool agreed) {
                if (agreed)
                    runCommand(id, command, arguments, context);
                else
                    finishRequest(id, failure(u"The person did not agree, so the command did not run."_s));
            });
        });
}

void Bridge::runCommand(const QJsonValue& id, const QJsonObject& command, const QJsonObject& arguments,
                        QObject* context)
{
    // Only the person's answer gives the command's confirmationParam, never the agent.
    const QString agreement = command.value(u"confirmationParam"_s).toString();
    QJsonObject given = arguments;
    if (!agreement.isEmpty())
        given.remove(agreement);
    const QString commandId = command.value(u"id"_s).toString();
    m_app.run(context, commandId, given,
              [this, id, agreement, given, commandId, context](const AppClient::Outcome& outcome) {
                  const auto* refused = outcome ? std::get_if<CommandError>(&*outcome) : nullptr;
                  if (refused && refused->code == u"needs_confirmation"_s && !agreement.isEmpty()) {
                      if (!m_canAsk) {
                          finishRequest(
                              id, failure(u"This run needs the person's agreement (“%1”), and this MCP client "
                                          "cannot ask for it. Ask the person to run it in Mikser Studio instead."_s.arg(
                                              refused->message)));
                          return;
                      }
                      ask(context, refused->message, [this, id, agreement, given, commandId, context](bool agreed) {
                          if (!agreed) {
                              finishRequest(id, failure(u"The person did not agree, so the command did not run."_s));
                              return;
                          }
                          QJsonObject agreedArguments = given;
                          agreedArguments.insert(agreement, true);
                          m_app.run(context, commandId, agreedArguments,
                                    [this, id](const AppClient::Outcome& rerun) { finish(id, rerun); });
                      });
                      return;
                  }
                  finish(id, outcome);
              });
}

void Bridge::finish(const QJsonValue& id, const AppClient::Outcome& outcome)
{
    if (!outcome)
        finishRequest(id, failure(outcome.error()));
    else if (const auto* refused = std::get_if<CommandError>(&*outcome))
        finishRequest(id, failure(u"%1 (%2)"_s.arg(refused->message, refused->code)));
    else
        finishRequest(id, success(std::get<QJsonObject>(*outcome)));
}

void Bridge::ask(QObject* context, const QString& confirmation, std::function<void(bool agreed)> answered)
{
    // A client that has gone cannot answer.
    if (m_closing) {
        answered(false);
        return;
    }
    const QString askId = u"ask-%1"_s.arg(++m_questionCount);
    m_questions.insert(askId, Question{.request = context, .answered = std::move(answered)});
    const QJsonObject schema{{u"type"_s, u"object"_s}, {u"properties"_s, QJsonObject{}}};
    m_send(QJsonObject{
        {u"jsonrpc"_s, u"2.0"_s},
        {u"id"_s, askId},
        {u"method"_s, u"elicitation/create"_s},
        {u"params"_s, QJsonObject{{u"message"_s, confirmation}, {u"requestedSchema"_s, schema}}},
    });
}

Results Bridge::results() const
{
    return m_protocol >= kStructuredSince ? Results::Structured : Results::TextOnly;
}

QJsonObject Bridge::success(const QJsonObject& result) const
{
    const QString text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    QJsonObject answer{{u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, text}}}}};
    if (results() == Results::Structured)
        answer.insert(u"structuredContent"_s, result);
    return answer;
}

QObject* Bridge::startRequest(const QJsonValue& id)
{
    // A client that reuses the id of a request still running gets the answer to the new one only.
    delete m_running.take(keyOf(id));
    auto* context = new QObject(this);
    m_running.insert(keyOf(id), context);
    return context;
}

void Bridge::finishRequest(const QJsonValue& id, const QJsonObject& result)
{
    endRequest(id);
    m_send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, result}});
}

void Bridge::failRequest(const QJsonValue& id, int code, const QString& message)
{
    endRequest(id);
    m_send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                       {u"id"_s, id},
                       {u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
}

void Bridge::endRequest(const QJsonValue& id)
{
    if (QObject* context = m_running.take(keyOf(id)))
        context->deleteLater();
    closeWhenIdle();
}

void Bridge::toolsChanged()
{
    if (m_ready)
        m_send(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/tools/list_changed"_s}});
}

void Bridge::closeWhenIdle()
{
    if (!m_closing || m_closed || !m_running.isEmpty())
        return;
    m_closed = true;
    m_closeTimer.stop();
    Q_EMIT closed();
}

}  // namespace mikserstudio::mcp
