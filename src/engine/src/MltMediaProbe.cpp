#include "mikserstudio/engine/MltMediaProbe.h"

#include "MltProfile.h"
#include "MltResource.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <Mlt.h>

#include <QByteArray>
#include <QDir>

#include <cmath>
#include <limits>
#include <numeric>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

constexpr int kMltDefaultLength = 15000;  // What MLT reports for producers without a natural end.

// Services that produce one still picture for any length.
bool isStillService(const QString& service)
{
    return service == u"color"_s || service == u"colour"_s || service == u"pixbuf"_s || service == u"qimage"_s ||
           service == u"pango"_s;
}

QByteArray streamKey(int index, const char* name)
{
    return "meta.media." + QByteArray::number(index) + '.' + name;
}

core::Rational rationalFromDouble(double fps)
{
    if (fps <= 0.0)
        return {};
    const double ntsc = fps * 1001.0 / 1000.0;
    if (std::abs(ntsc - std::round(ntsc)) < 0.005 && std::abs(fps - std::round(fps)) > 0.005)
        return {static_cast<int>(std::lround(ntsc)) * 1000, 1001};
    if (std::abs(fps - std::round(fps)) < 0.005)
        return {static_cast<int>(std::lround(fps)), 1};
    const int num = static_cast<int>(std::lround(fps * 1000.0));
    const int divisor = std::gcd(num, 1000);
    return {num / divisor, 1000 / divisor};
}

core::Rational nativeFrameRate(Mlt::Producer& producer, int videoIndex)
{
    const int num = producer.get_int("meta.media.frame_rate_num");
    const int den = producer.get_int("meta.media.frame_rate_den");
    if (num > 0 && den > 0) {
        const int divisor = std::gcd(num, den);
        return {num / divisor, den / divisor};
    }
    return rationalFromDouble(producer.get_double(streamKey(videoIndex, "stream.frame_rate").constData()));
}

}  // namespace

MltMediaProbe::MltMediaProbe(const MltRuntime& runtime)
    : m_runtime(runtime)
{
}

std::expected<core::MediaInfo, QString> MltMediaProbe::probe(const QString& path, core::Rational projectFrameRate)
{
    if (projectFrameRate.num <= 0 || projectFrameRate.den <= 0)
        return std::unexpected(
            u"The project frame rate %1/%2 is not valid."_s.arg(projectFrameRate.num).arg(projectFrameRate.den));

    auto profile = makeProfile(projectFrameRate, 1920, 1080);
    auto opened = openProducer(*profile, path);
    if (!opened)
        return std::unexpected(opened.error());
    Mlt::Producer& producer = **opened;

    // avformat fills in the exact frame rate and size only once a frame was read.
    const std::unique_ptr<Mlt::Frame> firstFrame(producer.get_frame());

    const QString service = QString::fromUtf8(producer.get("mlt_service"));
    core::MediaInfo info;
    info.durationFrames = producer.get_length();
    info.frameRate = projectFrameRate;

    if (isStillService(service)) {
        info.kind = core::MediaKind::Image;
        info.hasVideo = true;
        info.width = producer.get_int("meta.media.width");
        info.height = producer.get_int("meta.media.height");
        return info;
    }
    if (service != u"avformat"_s && service != u"avformat-novalidate"_s)
        return std::unexpected(u"\"%1\" is not a video, audio or image file (MLT opened it as \"%2\")."_s.arg(
            QDir::toNativeSeparators(path), service));

    const int videoIndex = producer.get_int("video_index");
    const int audioIndex = producer.get_int("audio_index");
    const int streams = producer.get_int("meta.media.nb_streams");
    info.hasVideo = videoIndex >= 0 && videoIndex < streams;
    info.hasAudio = audioIndex >= 0 && audioIndex < streams;
    if (!info.hasVideo && !info.hasAudio)
        return std::unexpected(
            u"\"%1\" contains no video or audio that Mikser Studio can play."_s.arg(QDir::toNativeSeparators(path)));

    if (info.hasVideo) {
        info.width = producer.get_int(streamKey(videoIndex, "codec.width").constData());
        info.height = producer.get_int(streamKey(videoIndex, "codec.height").constData());
        info.frameRate = nativeFrameRate(producer, videoIndex);
    }
    if (info.hasAudio) {
        info.audioSampleRate = producer.get_int(streamKey(audioIndex, "codec.sample_rate").constData());
        info.audioChannels = producer.get_int(streamKey(audioIndex, "codec.channels").constData());
    }

    // avformat marks single pictures as endlessly looping.
    const bool still = info.hasVideo && !info.hasAudio && producer.get_length() == std::numeric_limits<int>::max();
    if (still) {
        info.kind = core::MediaKind::Image;
        info.frameRate = projectFrameRate;
        info.durationFrames = kMltDefaultLength;
    } else {
        info.kind = info.hasVideo ? core::MediaKind::Video : core::MediaKind::Audio;
    }
    return info;
}

}  // namespace mikserstudio::engine
