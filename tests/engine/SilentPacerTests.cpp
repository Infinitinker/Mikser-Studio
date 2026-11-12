#include "EngineTestSupport.h"

#include "SilentPacer.h"

#include <thread>

using mikserstudio::core::Rational;
using mikserstudio::engine::SilentPacer;
using Clock = std::chrono::steady_clock;

TEST_CASE("The silent pacer spaces frames one frame period apart", "[pacer]")
{
    SilentPacer pacer(Rational{50, 1});  // 20 ms per frame.
    const auto start = Clock::now();
    for (int i = 0; i < 11; ++i)
        REQUIRE(pacer.waitUntilDue(1.0));
    const auto elapsed = Clock::now() - start;
    CHECK(elapsed >= std::chrono::milliseconds(195));
    CHECK(elapsed < std::chrono::milliseconds(600));
}

TEST_CASE("The silent pacer waits for a refresh while paused and can be canceled", "[pacer]")
{
    SilentPacer pacer(Rational{25, 1});

    std::thread refresher([&pacer] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pacer.refresh();
    });
    const auto start = Clock::now();
    CHECK(pacer.waitForRefresh());
    CHECK(Clock::now() - start >= std::chrono::milliseconds(40));
    refresher.join();

    std::thread canceler([&pacer] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pacer.cancel();
    });
    CHECK_FALSE(pacer.waitForRefresh());
    canceler.join();
    CHECK_FALSE(pacer.waitUntilDue(1.0));

    pacer.reset();
    pacer.refresh();
    CHECK(pacer.waitForRefresh());
}
