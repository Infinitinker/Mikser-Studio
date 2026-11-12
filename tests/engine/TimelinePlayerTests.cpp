#include "EngineTestSupport.h"

#include "mikserstudio/core/Project.h"
#include "mikserstudio/engine/TimelinePlayer.h"

#include <QSignalSpy>

#include <algorithm>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::core::Clip;
using mikserstudio::core::FramePos;
using mikserstudio::core::MediaItem;
using mikserstudio::core::MediaKind;
using mikserstudio::core::ProjectModel;
using mikserstudio::core::Rational;
using mikserstudio::core::Transport;
using mikserstudio::engine::AudioOutput;
using mikserstudio::engine::TimelinePlayer;
using mikserstudio::tests::centerColor;
using mikserstudio::tests::closeTo;
using mikserstudio::tests::RecordingSink;
using mikserstudio::tests::waitForFrameAt;
using mikserstudio::tests::waitUntil;

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 180;

// A small 25 fps project built from MLT color producers, played silently.
class TimelineFixture {
public:
    TimelineFixture() { project.reset(Rational{25, 1}, kWidth, kHeight); }

    QString addColor(const QString& color)
    {
        MediaItem item{
            .id = ProjectModel::newId(u"media"_s),
            .path = u"color:"_s + color,
            .name = color,
            .info = {.kind = MediaKind::Image,
                     .durationFrames = 125,
                     .width = 0,
                     .height = 0,
                     .frameRate = Rational{25, 1},
                     .hasVideo = true,
                     .hasAudio = false,
                     .audioSampleRate = 0,
                     .audioChannels = 0},
            .provenance = {},
        };
        REQUIRE(project.addMedia(item).has_value());
        return item.id;
    }

    QString addClip(const QString& trackId, const QString& mediaId, FramePos position, FramePos length)
    {
        const Clip clip{.id = ProjectModel::newId(u"clip"_s),
                        .mediaId = mediaId,
                        .trackId = trackId,
                        .position = position,
                        .in = 0,
                        .out = length};
        REQUIRE(project.addClip(clip).has_value());
        return clip.id;
    }

    void setTrackHidden(const QString& trackId, bool hidden)
    {
        const mikserstudio::core::Track* track = project.findTrack(trackId);
        REQUIRE(track != nullptr);
        REQUIRE(project.setTrackFlags(trackId, track->muted, hidden).has_value());
    }

    std::unique_ptr<TimelinePlayer> makePlayer()
    {
        auto player = std::make_unique<TimelinePlayer>(mikserstudio::tests::mltRuntime(), project, AudioOutput::Silent);
        player->setFrameSink(sink);
        return player;
    }

    ProjectModel project;
    std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>();
};

QColor colorAt(const RecordingSink& sink, FramePos position)
{
    const auto frame = waitForFrameAt(sink, position);
    if (!frame)
        FAIL("No frame was shown at position " << position);
    CHECK(frame->image.size() == QSize(kWidth, kHeight));
    CHECK(frame->image.format() == QImage::Format_RGBA8888);
    return centerColor(frame->image);
}

// Waits until the newest frame shown at `position` has `color`. Frames rendered before a change
// can still arrive just after it, so the first frame seen at a position proves nothing.
bool showsColorAt(const RecordingSink& sink, FramePos position, const QColor& color)
{
    return waitUntil([&] {
        const auto frame = sink.lastAt(position);
        return frame && closeTo(centerColor(frame->image), color);
    });
}

std::vector<FramePos> values(const QSignalSpy& positions)
{
    std::vector<FramePos> result;
    for (const QList<QVariant>& arguments : positions)
        result.push_back(arguments.first().toLongLong());
    return result;
}

}  // namespace

TEST_CASE("TimelinePlayer builds from color clips and reports the timeline duration", "[timeline]")
{
    TimelineFixture fixture;
    const QString red = fixture.addColor(u"#ff0000"_s);
    const QString blue = fixture.addColor(u"#0000ff"_s);
    fixture.addClip(u"V1"_s, red, 0, 50);
    fixture.addClip(u"V1"_s, blue, 50, 25);

    auto player = fixture.makePlayer();
    CHECK(player->duration() == 75);
    CHECK(player->frameRate() == Rational{25, 1});
    CHECK(player->state() == Transport::State::Paused);
    CHECK(player->position() == 0);
    CHECK(player->audioOutputName() == u"silent"_s);
    CHECK(closeTo(colorAt(*fixture.sink, 0), QColor(255, 0, 0)));
}

TEST_CASE("Seeking a paused timeline shows the frame at the new position", "[timeline]")
{
    TimelineFixture fixture;
    const QString red = fixture.addColor(u"#ff0000"_s);
    const QString blue = fixture.addColor(u"#0000ff"_s);
    fixture.addClip(u"V1"_s, red, 0, 50);
    fixture.addClip(u"V1"_s, blue, 50, 25);
    auto player = fixture.makePlayer();
    REQUIRE(waitForFrameAt(*fixture.sink, 0).has_value());

    QSignalSpy positions(player.get(), &Transport::positionChanged);
    player->seek(60);
    CHECK(player->position() == 60);
    CHECK(positions.count() == 1);
    CHECK(closeTo(colorAt(*fixture.sink, 60), QColor(0, 0, 255)));

    player->seek(10);
    CHECK(closeTo(colorAt(*fixture.sink, 10), QColor(255, 0, 0)));

    player->seek(500);  // Clamped to the last frame.
    CHECK(player->position() == 74);
    CHECK(closeTo(colorAt(*fixture.sink, 74), QColor(0, 0, 255)));
    CHECK(player->state() == Transport::State::Paused);
}

TEST_CASE("Upper video tracks are composited over lower ones; gaps are black", "[timeline]")
{
    TimelineFixture fixture;
    const QString red = fixture.addColor(u"#ff0000"_s);
    const QString green = fixture.addColor(u"#00ff00"_s);
    fixture.addClip(u"V1"_s, red, 0, 40);
    fixture.addClip(u"V2"_s, green, 20, 10);
    fixture.addClip(u"V1"_s, red, 50, 10);
    auto player = fixture.makePlayer();
    CHECK(player->duration() == 60);

    player->seek(25);
    CHECK(closeTo(colorAt(*fixture.sink, 25), QColor(0, 255, 0)));
    player->seek(35);
    CHECK(closeTo(colorAt(*fixture.sink, 35), QColor(255, 0, 0)));
    player->seek(45);
    CHECK(closeTo(colorAt(*fixture.sink, 45), QColor(0, 0, 0)));

    SECTION("a hidden upper track shows the track below")
    {
        QSignalSpy rebuilt(player.get(), &TimelinePlayer::rebuilt);
        fixture.setTrackHidden(u"V2"_s, true);
        REQUIRE(waitUntil([&] { return rebuilt.count() > 0; }));
        player->seek(25);
        CHECK(showsColorAt(*fixture.sink, 25, QColor(255, 0, 0)));
    }
}

TEST_CASE("Rebuilding after a model change keeps the position and play state", "[timeline]")
{
    TimelineFixture fixture;
    const QString red = fixture.addColor(u"#ff0000"_s);
    const QString green = fixture.addColor(u"#00ff00"_s);
    fixture.addClip(u"V1"_s, red, 0, 250);  // Long enough never to reach the end, even under load.
    auto player = fixture.makePlayer();
    QSignalSpy rebuilt(player.get(), &TimelinePlayer::rebuilt);

    player->seek(40);
    REQUIRE(closeTo(colorAt(*fixture.sink, 40), QColor(255, 0, 0)));

    SECTION("paused")
    {
        fixture.addClip(u"V2"_s, green, 30, 20);
        fixture.addClip(u"V2"_s, green, 260, 10);  // Two edits, one rebuild.
        REQUIRE(waitUntil([&] { return rebuilt.count() > 0; }));
        CHECK(rebuilt.count() == 1);
        CHECK(player->duration() == 270);
        CHECK(player->position() == 40);
        CHECK(player->state() == Transport::State::Paused);
        CHECK(showsColorAt(*fixture.sink, 40, QColor(0, 255, 0)));
    }

    SECTION("playing")
    {
        player->play();
        REQUIRE(player->state() == Transport::State::Playing);
        REQUIRE(waitUntil([&] { return player->position() > 45; }));
        QSignalSpy positions(player.get(), &Transport::positionChanged);
        const FramePos beforeEdit = player->position();
        fixture.addClip(u"V2"_s, green, 260, 10);
        REQUIRE(waitUntil([&] { return rebuilt.count() > 0; }));
        CHECK(player->duration() == 270);
        CHECK(player->state() == Transport::State::Playing);
        const FramePos atRebuild = player->position();
        CHECK(atRebuild >= beforeEdit);
        CHECK(waitUntil([&] { return player->position() > atRebuild + 5; }));
        player->pause();
        CHECK(std::ranges::is_sorted(values(positions)));  // Playback never went back.
    }
}

TEST_CASE("Many rebuilds while playing keep playback going", "[timeline]")
{
    TimelineFixture fixture;
    fixture.addClip(u"V1"_s, fixture.addColor(u"#ff0000"_s), 0, 1000);
    auto player = fixture.makePlayer();
    player->play();
    REQUIRE(waitUntil([&] { return player->position() > 5; }));

    QSignalSpy positions(player.get(), &Transport::positionChanged);
    for (int i = 0; i < 20; ++i) {
        player->rebuildNow();
        REQUIRE(player->state() == Transport::State::Playing);
        const FramePos now = player->position();
        REQUIRE(waitUntil([&] { return player->position() > now; }));
    }
    player->pause();
    CHECK(player->state() == Transport::State::Paused);
    CHECK(std::ranges::is_sorted(values(positions)));
}

TEST_CASE("Seeking while playing never reports frames from before the seek", "[timeline]")
{
    TimelineFixture fixture;
    fixture.addClip(u"V1"_s, fixture.addColor(u"#ff0000"_s), 0, 1000);
    auto player = fixture.makePlayer();
    player->play();

    // The MLT thread nearly always holds a frame rendered before the seek; it must not move the
    // playhead.
    for (int round = 0; round < 10; ++round) {
        QSignalSpy forward(player.get(), &Transport::positionChanged);
        player->seek(70);
        REQUIRE(waitUntil([&] { return player->position() > 72; }));
        for (const FramePos position : values(forward))
            CHECK(position >= 70);

        QSignalSpy back(player.get(), &Transport::positionChanged);
        player->seek(10);
        REQUIRE(waitUntil([&] { return player->position() > 12; }));
        for (const FramePos position : values(back))
            CHECK(position < 45);  // Reaching 45 from 10 takes well over a second.
    }
    player->pause();
}

TEST_CASE("Playing a clip again after it ended starts over", "[timeline]")
{
    TimelineFixture fixture;
    fixture.addClip(u"V1"_s, fixture.addColor(u"#ffffff"_s), 0, 10);
    auto player = fixture.makePlayer();

    for (int round = 0; round < 3; ++round) {
        const auto start = std::chrono::steady_clock::now();
        player->play();
        REQUIRE(waitUntil([&] { return player->state() == Transport::State::Paused; }));
        CHECK(player->position() == 9);
        // A stale frame from the end would pause at once; 9 frames at 25 fps take 360 ms.
        CHECK(std::chrono::steady_clock::now() - start >= 250ms);
    }
}

TEST_CASE("A new project frame rate is announced", "[timeline]")
{
    TimelineFixture fixture;
    auto player = fixture.makePlayer();
    QSignalSpy rebuilt(player.get(), &TimelinePlayer::rebuilt);
    QSignalSpy rates(player.get(), &Transport::frameRateChanged);

    fixture.project.reset(Rational{30, 1}, kWidth, kHeight);
    REQUIRE(waitUntil([&] { return rebuilt.count() == 1; }));
    CHECK(rates.count() == 1);
    CHECK(player->frameRate() == Rational{30, 1});

    fixture.project.reset(Rational{30, 1}, kWidth * 2, kHeight * 2);  // Same rate, new size.
    REQUIRE(waitUntil([&] { return rebuilt.count() == 2; }));
    CHECK(rates.count() == 1);
}

TEST_CASE("Project media naming an MLT service plays as a gap", "[timeline]")
{
    TimelineFixture fixture;
    MediaItem injected{
        .id = ProjectModel::newId(u"media"_s),
        .path = u"xml-string:<mlt><producer id=\"p\"><property name=\"resource\">color:#00ff00</property>"
                u"</producer></mlt>"_s,
        .name = u"injected"_s,
        .info = {.kind = MediaKind::Video,
                 .durationFrames = 100,
                 .width = 1920,
                 .height = 1080,
                 .frameRate = Rational{25, 1},
                 .hasVideo = true,
                 .hasAudio = false,
                 .audioSampleRate = 0,
                 .audioChannels = 0},
        .provenance = {},
    };
    REQUIRE(fixture.project.addMedia(injected).has_value());
    fixture.addClip(u"V1"_s, injected.id, 0, 30);

    auto player = std::make_unique<TimelinePlayer>(mikserstudio::tests::mltRuntime(), fixture.project,
                                                   AudioOutput::Silent);
    player->setFrameSink(fixture.sink);
    QSignalSpy errors(player.get(), &TimelinePlayer::errorOccurred);
    player->rebuildNow();
    REQUIRE(errors.count() == 1);
    CHECK(errors.first().first().toString().contains(u"not a file on this computer"_s));
    player->seek(10);
    CHECK(closeTo(colorAt(*fixture.sink, 10), QColor(0, 0, 0)));
}

TEST_CASE("Playback runs in real time and pauses on the last frame", "[timeline]")
{
    TimelineFixture fixture;
    fixture.addClip(u"V1"_s, fixture.addColor(u"#ffffff"_s), 0, 20);
    auto player = fixture.makePlayer();

    QSignalSpy states(player.get(), &Transport::stateChanged);
    const auto start = std::chrono::steady_clock::now();
    player->play();
    CHECK(player->state() == Transport::State::Playing);
    REQUIRE(waitUntil([&] { return player->state() == Transport::State::Paused; }, 5s));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(player->position() == 19);
    CHECK(elapsed >= 600ms);  // 19 frames at 25 fps is 760 ms.
    CHECK(states.count() == 2);

    // Playing from the end starts over; reverse playback runs back to the start.
    player->setSpeed(-2.0);
    CHECK(player->state() == Transport::State::Playing);
    REQUIRE(waitUntil([&] { return player->state() == Transport::State::Paused; }, 5s));
    CHECK(player->position() == 0);
}

TEST_CASE("An empty timeline shows black and cannot play", "[timeline]")
{
    TimelineFixture fixture;
    auto player = fixture.makePlayer();
    CHECK(player->duration() == 0);
    CHECK(player->state() == Transport::State::Paused);
    CHECK(closeTo(colorAt(*fixture.sink, 0), QColor(0, 0, 0)));
    player->play();
    CHECK(player->state() == Transport::State::Paused);
}

TEST_CASE("Media that cannot be opened plays as a gap and is reported", "[timeline]")
{
    TimelineFixture fixture;
    MediaItem missing{
        .id = ProjectModel::newId(u"media"_s),
        .path = u"D:/definitely/not/here.mp4"_s,
        .name = u"here.mp4"_s,
        .info = {.kind = MediaKind::Video,
                 .durationFrames = 100,
                 .width = 1920,
                 .height = 1080,
                 .frameRate = Rational{25, 1},
                 .hasVideo = true,
                 .hasAudio = false,
                 .audioSampleRate = 0,
                 .audioChannels = 0},
        .provenance = {},
    };
    REQUIRE(fixture.project.addMedia(missing).has_value());
    fixture.addClip(u"V1"_s, missing.id, 0, 30);
    fixture.addClip(u"V1"_s, fixture.addColor(u"#ff0000"_s), 30, 10);

    auto player = std::make_unique<TimelinePlayer>(mikserstudio::tests::mltRuntime(), fixture.project,
                                                   AudioOutput::Silent);
    player->setFrameSink(fixture.sink);
    QSignalSpy errors(player.get(), &TimelinePlayer::errorOccurred);
    player->rebuildNow();
    REQUIRE(errors.count() == 1);
    CHECK(errors.first().first().toString().contains(u"does not exist"_s));
    CHECK(player->duration() == 40);
    player->seek(10);
    CHECK(closeTo(colorAt(*fixture.sink, 10), QColor(0, 0, 0)));
    player->seek(35);
    CHECK(closeTo(colorAt(*fixture.sink, 35), QColor(255, 0, 0)));
}

TEST_CASE("The sample video plays on the timeline", "[timeline][sample]")
{
    const auto sample = mikserstudio::tests::sampleVideo();
    if (!sample)
        SKIP("The sample video " MIKSERSTUDIO_SAMPLE_VIDEO " is not available");

    TimelineFixture fixture;
    MediaItem video{
        .id = ProjectModel::newId(u"media"_s),
        .path = *sample,
        .name = u"sample"_s,
        .info = {.kind = MediaKind::Video,
                 .durationFrames = 750,
                 .width = 1920,
                 .height = 1080,
                 .frameRate = Rational{30, 1},
                 .hasVideo = true,
                 .hasAudio = true,
                 .audioSampleRate = 44100,
                 .audioChannels = 2},
        .provenance = {},
    };
    REQUIRE(fixture.project.addMedia(video).has_value());
    const Clip clip{.id = ProjectModel::newId(u"clip"_s),
                    .mediaId = video.id,
                    .trackId = u"V1"_s,
                    .position = 10,
                    .in = 250,
                    .out = 350};
    REQUIRE(fixture.project.addClip(clip).has_value());
    auto player = fixture.makePlayer();
    CHECK(player->duration() == 110);

    player->seek(60);
    const auto frame = waitForFrameAt(*fixture.sink, 60, 30s);
    REQUIRE(frame.has_value());
    CHECK(frame->image.size() == QSize(kWidth, kHeight));
    CHECK_FALSE(closeTo(centerColor(frame->image), QColor(0, 0, 0), 8));

    player->play();
    CHECK(waitUntil([&] { return player->position() > 70; }, 30s));
    player->pause();
}
