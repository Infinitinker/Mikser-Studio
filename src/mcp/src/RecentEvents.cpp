// Keeping the latest events and answering the recent_events tool.
#include "RecentEvents.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::mcp {

namespace {

constexpr double kLargestWholeNumber = 9'007'199'254'740'992.0;  // 2^53

}  // namespace

QString RecentEvents::toolName()
{
    return u"recent_events"_s;
}

QJsonObject RecentEvents::tool()
{
    const QString title = u"Recent events"_s;
    const QJsonObject schema{
        {u"type"_s, u"object"_s},
        {u"properties"_s,
         QJsonObject{{u"after"_s,
                      QJsonObject{{u"type"_s, u"integer"_s},
                                  {u"minimum"_s, 0},
                                  {u"description"_s,
                                   u"Only the events after this number: the latest number of an earlier call."_s}}}}},
        {u"additionalProperties"_s, false},
    };
    return QJsonObject{
        {u"name"_s, toolName()},
        {u"title"_s, title},
        {u"description"_s,
         u"What happened in Mikser Studio lately, newest last: the commands the person, the agents and plugins "
         "ran or that failed, undo and redo, and commands that plugins added or removed. Each event has a "
         "number; pass the latest number as after to see only what changed since. It keeps the last %1 events "
         "since this bridge started."_s.arg(kKept)},
        {u"inputSchema"_s, schema},
        {u"annotations"_s, QJsonObject{{u"title"_s, title},
                                       {u"readOnlyHint"_s, true},
                                       {u"destructiveHint"_s, false},
                                       {u"openWorldHint"_s, false}}},
    };
}

void RecentEvents::add(const QJsonObject& event, const QDateTime& received)
{
    ++m_latest;
    m_events.push_back(QJsonObject{
        {u"number"_s, m_latest}, {u"time"_s, received.toUTC().toString(Qt::ISODateWithMs)}, {u"event"_s, event}});
    if (m_events.size() > kKept)
        m_events.pop_front();
}

std::optional<QJsonObject> RecentEvents::result(const QJsonObject& arguments) const
{
    qint64 after = 0;
    if (arguments.contains(u"after"_s)) {
        const double given = arguments.value(u"after"_s).toDouble(-1);
        // Above 2^53 a JSON number is no longer a whole number for sure, and it would not fit in qint64.
        if (given < 0 || given > kLargestWholeNumber || std::floor(given) != given)
            return std::nullopt;
        after = static_cast<qint64>(given);
    }
    QJsonArray events;
    for (const QJsonObject& event : m_events) {
        if (event.value(u"number"_s).toInteger() > after)
            events.append(event);
    }
    return QJsonObject{{u"events"_s, events}, {u"latest"_s, m_latest}};
}

}  // namespace mikserstudio::mcp
