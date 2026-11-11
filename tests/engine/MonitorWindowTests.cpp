#include "EngineTestSupport.h"

#include "mikserstudio/engine/MonitorWindow.h"

using namespace Qt::StringLiterals;
using mikserstudio::engine::MonitorWindow;
using mikserstudio::engine::VideoFrame;
using mikserstudio::tests::waitUntil;

namespace {

VideoFrame frame(mikserstudio::core::FramePos position, QSize size = {64, 36})
{
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::magenta);
    return VideoFrame{.image = image, .position = position};
}

}  // namespace

TEST_CASE("The monitor window draws only the newest frame and counts the dropped ones", "[monitor]")
{
    MonitorWindow window;
    window.resize(320, 180);
    window.show();
    REQUIRE(waitUntil([&] { return window.isExposed(); }));

    const auto sink = window.frameSink();
    sink->presentFrame(frame(1));
    REQUIRE(waitUntil([&] { return window.framesPresented() == 1; }));
    CHECK(window.backendName() == u"Null"_s);

    // Three frames before the window gets to draw: only the last one is presented.
    sink->presentFrame(frame(2));
    sink->presentFrame(frame(3));
    sink->presentFrame(frame(4, {128, 72}));
    REQUIRE(waitUntil([&] { return window.framesPresented() == 2; }));
    CHECK(window.framesReceived() == 4);
    CHECK(window.framesDropped() == 2);

    const auto grabbed = window.grabFramebuffer();
    REQUIRE(grabbed.has_value());
    CHECK_FALSE(grabbed->isNull());  // The Null backend reports a fixed swapchain size.
}

TEST_CASE("A monitor frame sink outlives its window", "[monitor]")
{
    std::shared_ptr<mikserstudio::engine::FrameSink> sink;
    {
        MonitorWindow window;
        sink = window.frameSink();
    }
    sink->presentFrame(frame(1));  // Must not touch the destroyed window.
    SUCCEED();
}
