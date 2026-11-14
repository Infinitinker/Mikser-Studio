// Publishing the app's events, turning them into the API's JSON, and the inbox that coalesces
// state events for a subscriber that reads them on its own thread.
#include "mikserstudio/core/EventBus.h"

#include <QJsonArray>

#include <algorithm>
#include <iterator>
#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

// The payload field that tells two events of a coalescing topic apart, or empty when the topic
// doesn't coalesce.
QString coalescingKey(const QString& topic)
{
    if (topic == u"transport.state"_s)
        return u"transport"_s;
    if (topic == u"job.progress"_s)
        return u"jobId"_s;
    return {};
}

}  // namespace

QJsonObject toJson(const Event& event)
{
    QJsonObject json = event.payload;
    json.insert(u"type"_s, event.topic);
    if (!event.origins.isEmpty()) {
        json.insert(u"origin"_s, event.origins.constFirst());
        json.insert(u"origins"_s, QJsonArray::fromStringList(event.origins));
    }
    return json;
}

EventBus::EventBus(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<Event>();
}

EventBus::~EventBus() = default;

void EventBus::publish(const Event& event)
{
    Q_EMIT published(event);
}

EventInbox::EventInbox(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 1))
{
}

void EventInbox::push(Event event)
{
    const std::scoped_lock lock(m_mutex);
    if (const QString key = coalescingKey(event.topic); !key.isEmpty()) {
        const QJsonValue which = event.payload.value(key);
        const auto queued = std::ranges::find_if(m_events, [&](const Event& other) {
            return other.topic == event.topic && other.payload.value(key) == which;
        });
        if (queued != m_events.end()) {
            // The newer one goes to the back, after the events that came before it.
            m_events.erase(queued);
            m_events.push_back(std::move(event));
            return;
        }
    }
    if (m_events.size() >= m_capacity) {
        m_events.pop_front();
        ++m_dropped;
    }
    m_events.push_back(std::move(event));
}

std::optional<Event> EventInbox::take()
{
    const std::scoped_lock lock(m_mutex);
    if (m_events.empty())
        return std::nullopt;
    Event event = std::move(m_events.front());
    m_events.pop_front();
    return event;
}

std::vector<Event> EventInbox::takeAll()
{
    const std::scoped_lock lock(m_mutex);
    std::vector<Event> events(std::make_move_iterator(m_events.begin()), std::make_move_iterator(m_events.end()));
    m_events.clear();
    return events;
}

std::size_t EventInbox::size() const
{
    const std::scoped_lock lock(m_mutex);
    return m_events.size();
}

std::size_t EventInbox::dropped() const
{
    const std::scoped_lock lock(m_mutex);
    return m_dropped;
}

}  // namespace mikserstudio::core
