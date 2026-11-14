// The source clip player: it opens a color resource paused on its first frame, gives stills a finite length,
// keeps the current file when another cannot be opened, and plays, scrubs and shuttles the sample video.
#include "EngineTestSupport.h"

#include "mikserstudio/engine/ClipPlayer.h"

#include <QSet>
#include <QTemporaryDir>

#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::core::Rational;
using mikserstudio::core::Transport;
using mikserstudio::engine::AudioOutput;
using mikserstudio::engine::ClipPlayer;
using mikserstudio::tests::centerColor;
using mikserstudio::tests::RecordingSink;
using mikserstudio::tests::waitForFrameAt;
using mikserstudio::tests::waitUntil;

namespace {

qsizetype distinctColors(const QImage& image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += image.height() / 16) {
        for (int x = 0; x < image.width(); x += image.width() / 16)
            colors.insert(image.pixel(x, y));
    }
    return colors.size();
}

}  // namespace

TEST_CASE("ClipPlayer opens an MLT color resource paused on its first frame", "[clip]")
{
    ClipPlayer player(mikserstudio::tests::mltRuntime(), AudioOutput::Silent);
    auto sink = std::make_shared<RecordingSink>();
    player.setFrameSink(sink);
    CHECK(player.state() == Transport::State::Stopped);

    REQUIRE(player.openFile(u"color:#00ff00"_s).has_value());
    CHECK(player.state() == Transport::State::Paused);
    CHECK(player.filePath() == u"color:#00ff00"_s);
    CHECK(player.frameRate() == ClipPlayer::kFallbackFrameRate);
    CHECK(player.duration() > 0);
    const auto frame = waitForFrameAt(*sink, 0);
    REQUIRE(frame.has_value());
    CHECK(frame->image.size() == QSize(ClipPlayer::kFallbackWidth, ClipPlayer::kFallbackHeight));
    CHECK(centerColor(frame->image) == QColor(0, 255, 0));

    player.close();
    CHECK(player.state() == Transport::State::Stopped);
    CHECK(player.duration() == 0);
}

TEST_CASE("ClipPlayer gives stills a finite length", "[clip]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QImage picture(64, 48, QImage::Format_RGB32);
    picture.fill(QColor(255, 255, 0));
    const QString ppm = dir.filePath(u"still.ppm"_s);
    REQUIRE(picture.save(ppm));

    ClipPlayer player(mikserstudio::tests::mltRuntime(), AudioOutput::Silent);
    auto sink = std::make_shared<RecordingSink>();
    player.setFrameSink(sink);
    REQUIRE(player.openFile(ppm).has_value());
    CHECK(player.duration() == 15000);
    player.seek(14999);
    const auto frame = waitForFrameAt(*sink, 14999);
    REQUIRE(frame.has_value());
    // avformat decodes the picture through YUV, so allow a little rounding.
    CHECK(mikserstudio::tests::closeTo(centerColor(frame->image), QColor(255, 255, 0), 12));
}

TEST_CASE("ClipPlayer keeps the current file when another cannot be opened", "[clip]")
{
    ClipPlayer player(mikserstudio::tests::mltRuntime(), AudioOutput::Silent);
    REQUIRE(player.openFile(u"color:#ff0000"_s).has_value());
    const auto duration = player.duration();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto opened = player.openFile(dir.filePath(u"gone.mov"_s));
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().contains(u"does not exist"_s));
    CHECK(opened.error().contains(u"gone.mov"_s));
    CHECK(player.filePath() == u"color:#ff0000"_s);
    CHECK(player.duration() == duration);
    CHECK(player.state() == Transport::State::Paused);
}

TEST_CASE("ClipPlayer scrubs, plays and shuttles the sample video", "[clip][sample]")
{
    const auto sample = mikserstudio::tests::sampleVideo();
    if (!sample)
        SKIP("The sample video " MIKSERSTUDIO_SAMPLE_VIDEO " is not available");

    ClipPlayer player(mikserstudio::tests::mltRuntime(), AudioOutput::Silent);
    auto sink = std::make_shared<RecordingSink>();
    player.setFrameSink(sink);
    REQUIRE(player.openFile(*sample).has_value());
    CHECK(player.frameRate() == Rational{30, 1});
    CHECK(player.duration() > 29 * 30);

    player.seek(450);
    const auto frame = waitForFrameAt(*sink, 450, 30s);
    REQUIRE(frame.has_value());
    CHECK(frame->image.size() == QSize(1920, 1080));
    CHECK(distinctColors(frame->image) > 16);

    player.play();
    REQUIRE(waitUntil([&] { return player.position() > 460; }, 30s));
    player.setSpeed(-1.0);
    const auto turnedAt = player.position();
    REQUIRE(waitUntil([&] { return player.position() < turnedAt - 3; }, 30s));
    player.pause();
    CHECK(player.state() == Transport::State::Paused);
}
