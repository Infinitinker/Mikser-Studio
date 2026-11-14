// The frame mailbox: it keeps only the newest frame and counts the frames dropped.
#include "EngineTestSupport.h"

#include "FrameMailbox.h"

using mikserstudio::engine::FrameMailbox;
using mikserstudio::engine::VideoFrame;

namespace {

VideoFrame frameAt(mikserstudio::core::FramePos position)
{
    QImage image(4, 4, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    return VideoFrame{.image = image, .position = position};
}

}  // namespace

TEST_CASE("The frame mailbox keeps only the newest frame and counts drops", "[mailbox]")
{
    FrameMailbox mailbox;
    int notifications = 0;
    mailbox.setListener([&notifications] { ++notifications; });

    CHECK_FALSE(mailbox.take().has_value());
    mailbox.presentFrame(frameAt(1));
    mailbox.presentFrame(frameAt(2));
    mailbox.presentFrame(frameAt(3));
    CHECK(notifications == 1);  // One wake-up until the frame is taken.
    CHECK(mailbox.received() == 3);
    CHECK(mailbox.dropped() == 2);

    const auto taken = mailbox.take();
    REQUIRE(taken.has_value());
    CHECK(taken->position == 3);
    CHECK_FALSE(mailbox.take().has_value());

    mailbox.presentFrame(frameAt(4));
    CHECK(notifications == 2);
    CHECK(mailbox.dropped() == 2);

    mailbox.setListener({});
    mailbox.presentFrame(frameAt(5));
    CHECK(notifications == 2);
    CHECK(mailbox.dropped() == 3);
}
