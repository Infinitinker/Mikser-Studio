#include "EngineTestSupport.h"

#include "mikserstudio/engine/MltMediaProbe.h"

#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>

using namespace Qt::StringLiterals;
using mikserstudio::core::MediaKind;
using mikserstudio::core::Rational;
using mikserstudio::engine::MltMediaProbe;

namespace {

// Writes a 16-bit PCM WAV file of silence.
void writeWav(const QString& path, int sampleRate, int channels, int seconds)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto blockAlign = static_cast<quint16>(channels * 2);
    const auto byteRate = static_cast<quint32>(sampleRate) * blockAlign;
    const auto dataBytes = byteRate * static_cast<quint32>(seconds);
    out.writeRawData("RIFF", 4);
    out << static_cast<quint32>(36 + dataBytes);
    out.writeRawData("WAVEfmt ", 8);
    out << static_cast<quint32>(16) << static_cast<quint16>(1) << static_cast<quint16>(channels)
        << static_cast<quint32>(sampleRate) << byteRate << blockAlign << static_cast<quint16>(16);
    out.writeRawData("data", 4);
    out << dataBytes;
    const QByteArray silence(static_cast<qsizetype>(dataBytes), '\0');
    out.writeRawData(silence.constData(), static_cast<int>(silence.size()));
}

}  // namespace

TEST_CASE("Probing an MLT color resource gives a still image", "[probe]")
{
    MltMediaProbe probe(mikserstudio::tests::mltRuntime());
    const auto info = probe.probe(u"color:#ff0000"_s, Rational{25, 1});
    REQUIRE(info.has_value());
    CHECK(info->kind == MediaKind::Image);
    CHECK(info->hasVideo);
    CHECK_FALSE(info->hasAudio);
    CHECK(info->durationFrames > 0);
    CHECK(info->frameRate == Rational{25, 1});
}

TEST_CASE("Probing PNG, PPM and WAV files", "[probe]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    MltMediaProbe probe(mikserstudio::tests::mltRuntime());

    QImage picture(64, 48, QImage::Format_RGB32);
    picture.fill(Qt::blue);
    const QString png = dir.filePath(u"still.png"_s);
    REQUIRE(picture.save(png));
    const auto still = probe.probe(png, Rational{30, 1});
    REQUIRE(still.has_value());
    CHECK(still->kind == MediaKind::Image);
    CHECK(still->width == 64);
    CHECK(still->height == 48);

    // No dedicated MLT image producer handles PPM, so avformat opens it as an endless still.
    const QString ppm = dir.filePath(u"still.ppm"_s);
    REQUIRE(picture.save(ppm));
    const auto viaAvformat = probe.probe(ppm, Rational{30, 1});
    REQUIRE(viaAvformat.has_value());
    CHECK(viaAvformat->kind == MediaKind::Image);
    CHECK(viaAvformat->width == 64);
    CHECK(viaAvformat->durationFrames == 15000);

    const QString wav = dir.filePath(u"tone.wav"_s);
    writeWav(wav, 48000, 2, 2);
    const auto audio = probe.probe(wav, Rational{25, 1});
    REQUIRE(audio.has_value());
    CHECK(audio->kind == MediaKind::Audio);
    CHECK(audio->hasAudio);
    CHECK_FALSE(audio->hasVideo);
    CHECK(audio->audioSampleRate == 48000);
    CHECK(audio->audioChannels == 2);
    CHECK(audio->durationFrames == 50);
}

TEST_CASE("Probing the sample video converts its duration to the project frame rate", "[probe][sample]")
{
    const auto sample = mikserstudio::tests::sampleVideo();
    if (!sample)
        SKIP("The sample video " MIKSERSTUDIO_SAMPLE_VIDEO " is not available");

    MltMediaProbe probe(mikserstudio::tests::mltRuntime());
    const auto at30 = probe.probe(*sample, Rational{30, 1});
    REQUIRE(at30.has_value());
    CHECK(at30->kind == MediaKind::Video);
    CHECK(at30->hasVideo);
    CHECK(at30->hasAudio);
    CHECK(at30->width == 1920);
    CHECK(at30->height == 1080);
    CHECK(at30->frameRate == Rational{30, 1});
    CHECK(at30->audioSampleRate == 44100);
    CHECK(at30->audioChannels == 2);

    const auto at25 = probe.probe(*sample, Rational{25, 1});
    REQUIRE(at25.has_value());
    CHECK(at25->frameRate == Rational{30, 1});  // The media's own rate.
    const double seconds30 = static_cast<double>(at30->durationFrames) / 30.0;
    const double seconds25 = static_cast<double>(at25->durationFrames) / 25.0;
    CHECK(seconds30 > 29.0);
    CHECK(std::abs(seconds30 - seconds25) < 0.1);
}

TEST_CASE("Probe errors are explained in user terms", "[probe]")
{
    MltMediaProbe probe(mikserstudio::tests::mltRuntime());
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const auto notFound = probe.probe(dir.filePath(u"missing.mp4"_s), Rational{30, 1});
    REQUIRE_FALSE(notFound.has_value());
    CHECK(notFound.error().contains(u"does not exist"_s));
    CHECK(notFound.error().contains(u"missing.mp4"_s));

    const auto folder = probe.probe(dir.path(), Rational{30, 1});
    REQUIRE_FALSE(folder.has_value());
    CHECK(folder.error().contains(u"folder"_s));

    const QString garbage = dir.filePath(u"garbage.mp4"_s);
    QFile file(garbage);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(4096, 'x'));
    file.close();
    const auto unreadable = probe.probe(garbage, Rational{30, 1});
    REQUIRE_FALSE(unreadable.has_value());
    CHECK(unreadable.error().contains(u"garbage.mp4"_s));
}
