// The app's events: what happened, published on the main thread for the API, the MCP bridge and
// plugins to follow, and the bounded inbox a subscriber on another thread reads them from.
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QStringList>

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace mikserstudio::core {

// One event. `topic` names what happened ("command.executed", "clip.changed",
// "transport.state"), `payload` says what, with the ids of what changed, and `origins` is the
// chain of callers of the command that caused it (CallContext::origins), empty when the app
// itself did it.
struct Event {
    QString topic;
    QJsonObject payload;
    QStringList origins;
};

// The event as /v1/events sends it: the payload with "type" (the topic) and, when it has
// origins, "origin" (the first) and "origins".
[[nodiscard]] QJsonObject toJson(const Event& event);

// Publishes events to its subscribers, on the thread that owns it (the main thread). A
// subscriber on another thread connects to published() through an EventInbox.
class EventBus final : public QObject {
    Q_OBJECT

public:
    explicit EventBus(QObject* parent = nullptr);
    ~EventBus() override;

    void publish(const Event& event);

Q_SIGNALS:
    void published(const mikserstudio::core::Event& event);
};

// A subscriber's queue of events, for one that reads them on its own thread, such as a Lua
// plugin. It holds at most `capacity` events: when full, the oldest goes, and dropped() counts
// it. Events that only report the latest state coalesce: a new "transport.state" replaces the
// queued one of the same transport, and a new "job.progress" the queued one of the same job (the
// new one goes to the back), so a slow reader never falls behind on them. Safe to use from any
// thread.
class EventInbox final {
public:
    static constexpr std::size_t kDefaultCapacity = 1024;

    explicit EventInbox(std::size_t capacity = kDefaultCapacity);

    void push(Event event);
    [[nodiscard]] std::optional<Event> take();
    [[nodiscard]] std::vector<Event> takeAll();
    [[nodiscard]] std::size_t size() const;
    // How many events were dropped because the inbox was full.
    [[nodiscard]] std::size_t dropped() const;

private:
    mutable std::mutex m_mutex;
    std::deque<Event> m_events;
    std::size_t m_capacity;
    std::size_t m_dropped = 0;
};

}  // namespace mikserstudio::core

Q_DECLARE_METATYPE(mikserstudio::core::Event)
