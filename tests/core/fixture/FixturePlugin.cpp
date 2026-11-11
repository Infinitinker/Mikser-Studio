// Native test plugin for the PluginManager tests. Its id is "mikserstudio.tests.fixture" and its "mode"
// setting selects what activate() does:
//   "ok"                  registers .echo {text} -> {echo, greeting} and .run {command, params}
//   "fail_activate"       returns an error without registering anything
//   "register_then_fail"  registers .echo, then returns an error
//   "bad_prefix"          tries to register "other.plugin.command" and exposes the host's
//                         answer through .last_error -> {error}
// Every activate and deactivate is logged through the host as "fixture: activate <mode>" and
// "fixture: deactivate".

#include <mikserstudio/plugin_api.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <string_view>

using namespace Qt::StringLiterals;

namespace {

constexpr std::string_view kPrefix = "mikserstudio.tests.fixture";

struct State {
    const MikserStudioHostApiV1* host = nullptr;
    QByteArray lastError;
};

void collect(void* context, const char* utf8, size_t length)
{
    static_cast<QByteArray*>(context)->append(utf8, static_cast<qsizetype>(length));
}

void write(const MikserStudioStringSink* sink, const QByteArray& data)
{
    if (sink && sink->write)
        sink->write(sink->context, data.constData(), static_cast<size_t>(data.size()));
}

void log(const MikserStudioHostApiV1* host, const QByteArray& text)
{
    host->log(host->host, MIKSERSTUDIO_LOG_INFO, text.constData(), static_cast<size_t>(text.size()));
}

QJsonObject parse(const char* json, size_t length)
{
    return QJsonDocument::fromJson(QByteArray(json, static_cast<qsizetype>(length))).object();
}

QJsonObject settings(const MikserStudioHostApiV1* host)
{
    QByteArray json;
    const MikserStudioStringSink sink{&json, &collect};
    host->get_settings(host->host, &sink);
    return QJsonDocument::fromJson(json).object();
}

int echo(void* userData, const char* params, size_t length, const MikserStudioStringSink* result, const MikserStudioStringSink*)
{
    const auto* state = static_cast<const State*>(userData);
    const QJsonObject reply{
        {u"echo"_s, parse(params, length).value(u"text")},
        {u"greeting"_s, settings(state->host).value(u"greeting")},
    };
    write(result, QJsonDocument(reply).toJson(QJsonDocument::Compact));
    return 0;
}

int run(void* userData, const char* params, size_t length, const MikserStudioStringSink* result, const MikserStudioStringSink* error)
{
    const auto* state = static_cast<const State*>(userData);
    const QJsonObject input = parse(params, length);
    const QByteArray id = input.value(u"command").toString().toUtf8();
    const QByteArray inner = QJsonDocument(input.value(u"params").toObject()).toJson(QJsonDocument::Compact);
    return state->host->execute_command(state->host->host, id.constData(), static_cast<size_t>(id.size()),
                                        inner.constData(), static_cast<size_t>(inner.size()), result, error);
}

int lastError(void* userData, const char*, size_t, const MikserStudioStringSink* result, const MikserStudioStringSink*)
{
    const auto* state = static_cast<const State*>(userData);
    write(result, QJsonDocument(QJsonObject{{u"error"_s, QString::fromUtf8(state->lastError)}}).toJson(QJsonDocument::Compact));
    return 0;
}

int registerCommand(State* state, const QJsonObject& spec, MikserStudioCommandFn fn)
{
    const QByteArray json = QJsonDocument(spec).toJson(QJsonDocument::Compact);
    const MikserStudioStringSink sink{&state->lastError, &collect};
    return state->host->register_command(state->host->host, json.constData(), static_cast<size_t>(json.size()), fn,
                                         state, &sink);
}

QJsonObject spec(std::string_view name, const QJsonObject& paramsSchema = {})
{
    QJsonObject result{
        {u"id"_s, QString::fromUtf8(kPrefix) + u'.' + QString::fromUtf8(name)},
        {u"title"_s, QString::fromUtf8(name)},
    };
    if (!paramsSchema.isEmpty())
        result.insert(u"paramsSchema"_s, paramsSchema);
    return result;
}

QJsonObject echoSchema()
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"text"_s, QJsonObject{{u"type"_s, u"string"_s}}}}},
        {u"required"_s, QJsonArray{u"text"_s}},
    };
}

int activate(const MikserStudioHostApiV1* host, void** pluginState, const MikserStudioStringSink* error)
{
    const QString mode = settings(host).value(u"mode").toString();
    log(host, "fixture: activate " + mode.toUtf8());
    auto* state = new State{host, {}};

    if (mode == u"fail_activate") {
        delete state;
        write(error, "fixture refused to start");
        return 1;
    }
    if (mode == u"register_then_fail") {
        registerCommand(state, spec("echo", echoSchema()), &echo);
        delete state;
        write(error, "fixture failed after registering");
        return 1;
    }
    if (mode == u"bad_prefix") {
        const QJsonObject foreign{{u"id"_s, u"other.plugin.command"_s}, {u"title"_s, u"Foreign"_s}};
        if (registerCommand(state, foreign, &echo) == 0) {
            delete state;
            write(error, "the host accepted a foreign command id");
            return 1;
        }
        registerCommand(state, spec("last_error"), &lastError);
        *pluginState = state;
        return 0;
    }
    QJsonObject echoSpec = spec("echo", echoSchema());
    echoSpec.insert(u"readOnly"_s, true);
    registerCommand(state, echoSpec, &echo);
    const QJsonObject runSchema{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"command"_s, QJsonObject{{u"type"_s, u"string"_s}}},
                                      {u"params"_s, QJsonObject{{u"type"_s, u"object"_s}}}}},
        {u"required"_s, QJsonArray{u"command"_s}},
    };
    QJsonObject runSpec = spec("run", runSchema);
    runSpec.insert(u"confirmation"_s, u"Run another command?"_s);
    registerCommand(state, runSpec, &run);
    *pluginState = state;
    return 0;
}

void deactivate(void* pluginState)
{
    auto* state = static_cast<State*>(pluginState);
    log(state->host, "fixture: deactivate");
    delete state;
}

constexpr MikserStudioPluginV1 kPlugin{sizeof(MikserStudioPluginV1), MIKSERSTUDIO_PLUGIN_API_VERSION, &activate, &deactivate};

}  // namespace

extern "C" MIKSERSTUDIO_PLUGIN_EXPORT const MikserStudioPluginV1* mikserstudio_plugin_v1(void)
{
    return &kPlugin;
}
