#include "EngineTestSupport.h"

#include "Shuttle.h"

using mikserstudio::engine::nextShuttleSpeed;
using mikserstudio::engine::ShuttleKey;

TEST_CASE("L and J double the shuttle speed up to 8x, K stops", "[shuttle]")
{
    CHECK(nextShuttleSpeed(0.0, ShuttleKey::Forward) == 1.0);
    CHECK(nextShuttleSpeed(1.0, ShuttleKey::Forward) == 2.0);
    CHECK(nextShuttleSpeed(4.0, ShuttleKey::Forward) == 8.0);
    CHECK(nextShuttleSpeed(8.0, ShuttleKey::Forward) == 8.0);
    CHECK(nextShuttleSpeed(-4.0, ShuttleKey::Forward) == 1.0);

    CHECK(nextShuttleSpeed(0.0, ShuttleKey::Reverse) == -1.0);
    CHECK(nextShuttleSpeed(-1.0, ShuttleKey::Reverse) == -2.0);
    CHECK(nextShuttleSpeed(-8.0, ShuttleKey::Reverse) == -8.0);
    CHECK(nextShuttleSpeed(2.0, ShuttleKey::Reverse) == -1.0);

    CHECK(nextShuttleSpeed(4.0, ShuttleKey::Stop) == 0.0);
    CHECK(nextShuttleSpeed(-2.0, ShuttleKey::Stop) == 0.0);
}
