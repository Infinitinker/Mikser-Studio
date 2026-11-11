#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

class FakeTransport final : public Transport {
public:
    [[nodiscard]] FramePos position() const override { return m_position; }
    [[nodiscard]] FramePos duration() const override { return 100; }
    [[nodiscard]] State state() const override { return m_state; }
    [[nodiscard]] Rational frameRate() const override { return {25, 1}; }
    [[nodiscard]] double speed() const override { return m_state == State::Playing ? m_speed : 0.0; }

    void play() override
    {
        m_speed = 1.0;
        setState(State::Playing);
    }
    void pause() override { setState(State::Paused); }
    void seek(FramePos frame) override
    {
        m_position = std::clamp<FramePos>(frame, 0, duration() - 1);
        seeks.append(frame);
        Q_EMIT positionChanged(m_position);
    }
    void setSpeed(double value) override
    {
        m_speed = value;
        setState(value == 0.0 ? State::Paused : State::Playing);
    }

    QList<FramePos> seeks;

private:
    void setState(State state)
    {
        m_state = state;
        Q_EMIT stateChanged(state);
    }

    FramePos m_position = 0;
    State m_state = State::Stopped;
    double m_speed = 0.0;
};

struct Fixture {
    Fixture() { registerTransportCommands(bus, transport, u"program"_s, u"Program Monitor"_s); }

    QJsonObject run(const QString& name, const QJsonObject& params = {})
    {
        const auto result = bus.execute(u"program."_s + name, params);
        REQUIRE(result);
        return *result;
    }

    UndoStack stack;
    CommandBus bus{stack};
    FakeTransport transport;
};

}  // namespace

TEST_CASE("Transport commands are registered under the prefix", "[transport]")
{
    Fixture f;
    for (const QString& name : {u"play"_s, u"pause"_s, u"toggle"_s, u"seek"_s, u"step"_s, u"shuttle"_s, u"status"_s}) {
        const CommandSpec* spec = f.bus.spec(u"program."_s + name);
        REQUIRE(spec);
        CHECK(spec->category == u"Program Monitor"_s);
        CHECK(spec->title.startsWith(u"Program Monitor: "_s));
        CHECK_FALSE(spec->undoable);
        CHECK(spec->readOnly == (name == u"status"_s));
        CHECK(spec->confirmation.isEmpty());
    }
    registerTransportCommands(f.bus, f.transport, u"source"_s, u"Source Monitor"_s);
    CHECK(f.bus.contains(u"source.play"_s));
}

TEST_CASE("Transport commands drive the transport and return its status", "[transport]")
{
    Fixture f;
    CHECK(f.run(u"status"_s) == json(R"({"position": 0, "duration": 100, "state": "stopped", "frameRate": {"num": 25, "den": 1}})"));

    CHECK(f.run(u"play"_s).value(u"state").toString() == u"playing"_s);
    CHECK(f.run(u"pause"_s).value(u"state").toString() == u"paused"_s);
    CHECK(f.run(u"toggle"_s).value(u"state").toString() == u"playing"_s);
    CHECK(f.run(u"toggle"_s).value(u"state").toString() == u"paused"_s);

    CHECK(f.run(u"seek"_s, json(R"({"frame": 42})")).value(u"position").toInt() == 42);
    const auto negative = f.bus.execute(u"program.seek"_s, json(R"({"frame": -1})"));
    REQUIRE_FALSE(negative);
    CHECK(negative.error().code == CommandErrorCode::InvalidParams);
    CHECK_FALSE(f.bus.execute(u"program.seek"_s));

    CHECK(f.run(u"shuttle"_s, json(R"({"speed": -2})")).value(u"state").toString() == u"playing"_s);
    CHECK(f.transport.speed() == -2.0);
    CHECK_FALSE(f.bus.execute(u"program.shuttle"_s, json(R"({"speed": 100})")));
    CHECK(f.run(u"shuttle"_s, json(R"({"speed": 0})")).value(u"state").toString() == u"paused"_s);
}

TEST_CASE("Stepping pauses and clamps to the media", "[transport]")
{
    Fixture f;
    f.run(u"seek"_s, json(R"({"frame": 10})"));
    f.run(u"play"_s);
    const QJsonObject stepped = f.run(u"step"_s);
    CHECK(stepped.value(u"position").toInt() == 11);
    CHECK(stepped.value(u"state").toString() == u"paused"_s);
    CHECK(f.run(u"step"_s, json(R"({"frames": -5})")).value(u"position").toInt() == 6);
    CHECK(f.run(u"step"_s, json(R"({"frames": -50})")).value(u"position").toInt() == 0);
    CHECK(f.run(u"step"_s, json(R"({"frames": 500})")).value(u"position").toInt() == 99);
    CHECK(f.transport.seeks.last() == 99);
    CHECK_FALSE(f.bus.execute(u"program.step"_s, json(R"({"frames": 0.5})")));
}

TEST_CASE("Frame params outside the supported range are rejected, not wrapped", "[transport]")
{
    Fixture f;
    f.run(u"seek"_s, json(R"({"frame": 10})"));
    for (const char* params : {R"({"frame": 1e19})", R"({"frame": 1099511627777})"}) {
        const auto seek = f.bus.execute(u"program.seek"_s, json(params));
        REQUIRE_FALSE(seek);
        CHECK(seek.error().code == CommandErrorCode::InvalidParams);
    }
    for (const char* params : {R"({"frames": 9223372036854775000})", R"({"frames": -1e19})"}) {
        const auto step = f.bus.execute(u"program.step"_s, json(params));
        REQUIRE_FALSE(step);
        CHECK(step.error().code == CommandErrorCode::InvalidParams);
    }
    CHECK(f.transport.seeks == QList<FramePos>{10});

    CHECK(f.run(u"seek"_s, QJsonObject{{u"frame"_s, qint64{kMaxFrame}}}).value(u"position").toInt() == 99);
    CHECK(f.run(u"step"_s, QJsonObject{{u"frames"_s, qint64{kMaxFrame}}}).value(u"position").toInt() == 99);
    CHECK(f.run(u"step"_s, QJsonObject{{u"frames"_s, qint64{-kMaxFrame}}}).value(u"position").toInt() == 0);
}
