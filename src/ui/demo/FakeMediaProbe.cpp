// Describing a media item by its extension and name: kind, duration, frame rate and size.
#include "FakeMediaProbe.h"

#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui::demo {

std::expected<core::MediaInfo, QString> FakeMediaProbe::probe(const QString& path, core::Rational projectFrameRate)
{
    const QFileInfo file(path);
    const QString suffix = file.suffix().toLower();
    static const QStringList video{u"mp4"_s, u"mov"_s, u"mkv"_s, u"webm"_s, u"avi"_s, u"mxf"_s, u"m4v"_s};
    static const QStringList audio{u"wav"_s, u"mp3"_s, u"flac"_s, u"aac"_s, u"ogg"_s, u"m4a"_s, u"opus"_s};
    static const QStringList image{u"png"_s, u"jpg"_s, u"jpeg"_s, u"tif"_s, u"tiff"_s, u"bmp"_s, u"webp"_s, u"exr"_s};

    const double fps = projectFrameRate.toDouble() > 0.0 ? projectFrameRate.toDouble() : 30.0;
    const double seconds = 8.0 + static_cast<double>(qHash(file.completeBaseName()) % 23U);
    core::MediaInfo info;
    info.frameRate = projectFrameRate;
    info.durationFrames = static_cast<core::FramePos>(std::lround(seconds * fps));
    if (video.contains(suffix)) {
        info.kind = core::MediaKind::Video;
        info.width = 1920;
        info.height = 1080;
        info.hasVideo = true;
        info.hasAudio = true;
        info.audioSampleRate = 48000;
        info.audioChannels = 2;
    } else if (audio.contains(suffix)) {
        info.kind = core::MediaKind::Audio;
        info.hasAudio = true;
        info.audioSampleRate = 48000;
        info.audioChannels = 2;
    } else if (image.contains(suffix)) {
        info.kind = core::MediaKind::Image;
        info.width = 1920;
        info.height = 1080;
        info.hasVideo = true;
    } else {
        return std::unexpected(u"The demo cannot read .%1 files; use a video, audio or image file name."_s.arg(suffix));
    }
    return info;
}

}  // namespace mikserstudio::ui::demo
