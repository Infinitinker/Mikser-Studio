// The playback commands (play, pause, toggle, seek, step, shuttle, status) for one transport.
#include "mikserstudio/core/Transport.h"

#include "Logging.h"
#include "ProjectJson.h"
#include "mikserstudio/core/CommandBus.h"

#include <QJsonArray>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

QString toString(Transport::State state)
{
    switch (state) {
    case Transport::State::Stopped:
        return u"stopped"_s;
    case Transport::State::Paused:
        return u"paused"_s;
    case Transport::State::Playing:
        return u"playing"_s;
    }
    return u"stopped"_s;
}

QJsonObject status(const Transport& transport)
{
    return QJsonObject{
        {u"position"_s, transport.position()},
        {u"duration"_s, transport.duration()},
        {u"state"_s, toString(transport.state())},
        {u"frameRate"_s, rationalToJson(transport.frameRate())},
    };
}

QJsonObject frameSchema(qint64 minimum)
{
    return QJsonObject{{u"type"_s, u"integer"_s}, {u"minimum"_s, minimum}, {u"maximum"_s, qint64{kMaxFrame}}};
}

QJsonObject withDefault(QJsonObject schema, const QJsonValue& value)
{
    schema.insert(u"default"_s, value);
    return schema;
}

QJsonObject paramsSchema(const QJsonObject& properties, const QStringList& required)
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, properties},
        {u"required"_s, QJsonArray::fromStringList(required)},
        {u"additionalProperties"_s, false},
    };
}

// position + frames clamped to [0, last], without overflowing for any inputs.
FramePos steppedFrame(FramePos position, FramePos frames, FramePos last)
{
    const FramePos from = std::clamp<FramePos>(position, 0, last);
    if (frames >= 0)
        return frames > last - from ? last : from + frames;
    return frames < -from ? 0 : from + frames;
}

struct TransportCommand {
    QString name;
    QString title;
    QString description;
    QJsonObject schema;
    std::function<void(Transport&, const QJsonObject&)> run;
    bool readOnly = false;
};

QList<TransportCommand> transportCommands()
{
    return {
        {u"play"_s,
         u"Play"_s,
         u"Starts playback at normal speed."_s,
         {},
         [](Transport& t, const QJsonObject&) { t.play(); }},
        {u"pause"_s, u"Pause"_s, u"Pauses playback."_s, {}, [](Transport& t, const QJsonObject&) { t.pause(); }},
        {u"toggle"_s,
         u"Play/pause"_s,
         u"Starts or pauses playback."_s,
         {},
         [](Transport& t, const QJsonObject&) {
             if (t.state() == Transport::State::Playing)
                 t.pause();
             else
                 t.play();
         }},
        {u"seek"_s, u"Go to frame"_s, u"Moves the playhead to a frame."_s,
         paramsSchema(QJsonObject{{u"frame"_s, frameSchema(0)}}, {u"frame"_s}),
         [](Transport& t, const QJsonObject& params) { t.seek(params.value(u"frame").toInteger()); }},
        {u"step"_s, u"Step frames"_s, u"Pauses and moves the playhead by a number of frames (negative steps back)."_s,
         paramsSchema(QJsonObject{{u"frames"_s, withDefault(frameSchema(-kMaxFrame), 1)}}, {}),
         [](Transport& t, const QJsonObject& params) {
             if (t.state() == Transport::State::Playing)
                 t.pause();
             const FramePos last = std::max<FramePos>(0, t.duration() - 1);
             t.seek(steppedFrame(t.position(), params.value(u"frames").toInteger(), last));
         }},
        {u"shuttle"_s, u"Shuttle"_s, u"Plays at a speed: 1 is normal, negative plays backwards, 0 pauses."_s,
         paramsSchema(
             QJsonObject{{u"speed"_s, QJsonObject{{u"type"_s, u"number"_s}, {u"minimum"_s, -32}, {u"maximum"_s, 32}}}},
             {u"speed"_s}),
         [](Transport& t, const QJsonObject& params) { t.setSpeed(params.value(u"speed").toDouble()); }},
        {u"status"_s,
         u"Playback status"_s,
         u"Returns position, duration, state and frame rate."_s,
         {},
         [](Transport&, const QJsonObject&) { },
         true},
    };
}

}  // namespace

void registerTransportCommands(CommandBus& bus, Transport& transport, const QString& prefix, const QString& displayName)
{
    for (TransportCommand& command : transportCommands()) {
        const bool registered = bus.registerCommand(
            CommandSpec{
                .id = prefix + u'.' + command.name,
                .title = u"%1: %2"_s.arg(displayName, command.title),
                .description = command.description,
                .category = displayName,
                .paramsSchema = command.schema,
                .shortcut = {},
                .undoable = false,
                .readOnly = command.readOnly,
                .confirmation = {},
            },
            [&transport, run = std::move(command.run)](const QJsonObject& params) -> CommandResult {
                run(transport, params);
                return CommandOutcome{status(transport), nullptr};
            });
        if (!registered)
            qCWarning(lcTransport).noquote()
                << "Transport command" << prefix + u'.' + command.name << "was not registered";
    }
}

}  // namespace mikserstudio::core
