// The core's events: their JSON, the inbox that coalesces state events, and the events the bus and
// the project publish with their origins and ids.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>

#include <ranges>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;

namespace {

class NoProbe final : public MediaProbe {
public:
    std::expected<MediaInfo, QString> probe(const QString&, Rational) override
    {
        return std::unexpected(u"no probe in this test"_s);
    }
};

// The events a bus publishes, in order.
struct Recorder {
    explicit Recorder(CommandBus& bus)
    {
        QObject::connect(&bus.events(), &EventBus::published, &bus,
                         [this](const Event& event) { events.append(event); });
    }

    [[nodiscard]] QStringList topics() const
    {
        QStringList names;
        for (const Event& event : events)
            names.append(event.topic);
        return names;
    }

    [[nodiscard]] const Event& last(const QString& topic) const
    {
        const auto found = std::ranges::find(events | std::views::reverse, topic, &Event::topic);
        REQUIRE(found != (events | std::views::reverse).end());
        return *found;
    }

    QList<Event> events;
};

Event progress(const QString& jobId, int percent)
{
    return {u"job.progress"_s, QJsonObject{{u"jobId"_s, jobId}, {u"percent"_s, percent}}, {}};
}

}  // namespace

TEST_CASE("An event's JSON is its payload with its type and origins", "[events]")
{
    const QJsonObject json = toJson(
        {u"clip.changed"_s, QJsonObject{{u"clipId"_s, u"clip-1"_s}}, {u"api"_s, u"plugin:hello"_s}});
    CHECK(json.value(u"type"_s) == u"clip.changed"_s);
    CHECK(json.value(u"clipId"_s) == u"clip-1"_s);
    CHECK(json.value(u"origin"_s) == u"api"_s);
    CHECK(json.value(u"origins"_s).toArray() == QJsonArray{u"api"_s, u"plugin:hello"_s});

    const QJsonObject bare = toJson(Event{u"project.reset"_s, {}, {}});
    CHECK(bare == QJsonObject{{u"type"_s, u"project.reset"_s}});
}

TEST_CASE("An inbox coalesces state events and drops the oldest when full", "[events]")
{
    EventInbox inbox(4);
    inbox.push({u"clip.added"_s, QJsonObject{{u"clipId"_s, u"clip-1"_s}}, {}});
    inbox.push(progress(u"job-1"_s, 10));
    inbox.push(progress(u"job-2"_s, 50));
    inbox.push(progress(u"job-1"_s, 20));  // Replaces job-1's 10, and goes to the back.
    REQUIRE(inbox.size() == 3);
    inbox.push({u"transport.state"_s, QJsonObject{{u"transport"_s, u"program"_s}, {u"position"_s, 1}}, {}});
    inbox.push({u"transport.state"_s, QJsonObject{{u"transport"_s, u"program"_s}, {u"position"_s, 2}}, {}});
    CHECK(inbox.size() == 4);
    CHECK(inbox.dropped() == 0);

    const std::vector<Event> events = inbox.takeAll();
    REQUIRE(events.size() == 4);
    CHECK(events[0].topic == u"clip.added"_s);
    CHECK(events[1].payload.value(u"jobId"_s) == u"job-2"_s);
    CHECK(events[2].payload.value(u"percent"_s) == 20);
    CHECK(events[3].payload.value(u"position"_s) == 2);
    CHECK(inbox.size() == 0);
    CHECK_FALSE(inbox.take().has_value());

    for (int i = 0; i < 6; ++i)
        inbox.push({u"clip.changed"_s, QJsonObject{{u"clipId"_s, u"clip-%1"_s.arg(i)}}, {}});
    CHECK(inbox.size() == 4);
    CHECK(inbox.dropped() == 2);
    const std::optional<Event> oldest = inbox.take();
    REQUIRE(oldest.has_value());
    CHECK(oldest->payload.value(u"clipId"_s) == u"clip-2"_s);
}

TEST_CASE("The bus publishes its commands' runs and the undo history with their origins", "[events]")
{
    UndoStack stack;
    CommandBus bus(stack);
    Recorder recorder(bus);
    NoProbe probe;
    ProjectModel project;
    registerProjectCommands(bus, project, probe);
    CHECK(recorder.topics().contains(u"command.registered"_s));
    CHECK(recorder.last(u"command.registered"_s).origins.isEmpty());
    recorder.events.clear();

    const auto added = bus.execute(u"timeline.add_track"_s, QJsonObject{{u"kind"_s, u"video"_s}}, CallContext::api());
    REQUIRE(added);
    CHECK(recorder.topics() == QStringList{u"track.added"_s, u"undo.changed"_s, u"command.executed"_s});
    const Event& track = recorder.last(u"track.added"_s);
    CHECK(track.payload == QJsonObject{{u"trackId"_s, added->value(u"trackId"_s)}});
    CHECK(track.origins == QStringList{u"api"_s});
    CHECK(recorder.last(u"undo.changed"_s).payload.value(u"canUndo"_s).toBool());
    const Event& executed = recorder.last(u"command.executed"_s);
    CHECK(executed.payload.value(u"id"_s) == u"timeline.add_track"_s);
    CHECK(executed.payload.value(u"result"_s).toObject() == *added);
    CHECK(executed.origins == QStringList{u"api"_s});

    recorder.events.clear();
    REQUIRE(bus.execute(u"edit.undo"_s));
    CHECK(recorder.topics() == QStringList{u"track.removed"_s, u"undo.changed"_s, u"command.executed"_s});
    CHECK(recorder.last(u"track.removed"_s).origins == QStringList{u"ui"_s});

    recorder.events.clear();
    REQUIRE_FALSE(bus.execute(u"timeline.remove_clip"_s, QJsonObject{{u"clipId"_s, u"clip-none"_s}}));
    REQUIRE(recorder.topics() == QStringList{u"command.failed"_s});
    CHECK(recorder.events.constFirst().payload.value(u"error"_s).toObject().value(u"code"_s) == u"failed"_s);

    recorder.events.clear();
    project.reset();
    CHECK(recorder.topics().contains(u"project.reset"_s));
    CHECK(recorder.last(u"project.reset"_s).origins.isEmpty());
}
