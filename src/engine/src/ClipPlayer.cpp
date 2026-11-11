#include "mikserstudio/engine/ClipPlayer.h"

#include "Logging.h"
#include "MltResource.h"
#include "mikserstudio/engine/MltMediaProbe.h"

#include <Mlt.h>

#include <algorithm>

namespace mikserstudio::engine {

ClipPlayer::ClipPlayer(const MltRuntime& runtime, AudioOutput audio, QObject* parent)
    : MltPlayer(runtime, audio, parent)
{
}

ClipPlayer::~ClipPlayer() = default;

std::expected<void, QString> ClipPlayer::openFile(const QString& path)
{
    MltMediaProbe probe(runtime());
    auto info = probe.probe(path, kFallbackFrameRate);
    if (!info)
        return std::unexpected(info.error());

    const bool video = info->kind == core::MediaKind::Video && info->width > 0 && info->height > 0;
    const core::Rational frameRate = video ? info->frameRate : kFallbackFrameRate;
    const int width = video ? info->width : kFallbackWidth;
    const int height = video ? info->height : kFallbackHeight;

    // The probe has opened the file already, so this only fails if it changes in between.
    configureProfile(frameRate, width, height);
    auto producer = openProducer(profile(), path);
    if (!producer)
        return std::unexpected(producer.error());
    (*producer)->set("eof", "pause");
    if (info->kind == core::MediaKind::Image) {
        // Stills have no natural length (avformat reports INT_MAX); use the probe's.
        const int length = static_cast<int>(std::clamp<core::FramePos>(info->durationFrames, 1, 1 << 30));
        (*producer)->set("length", length);
        (*producer)->set_in_and_out(0, length - 1);
    }

    const auto duration = static_cast<core::FramePos>((*producer)->get_length());
    m_path = path;
    load(std::move(*producer), duration, 0, 0.0);
    qCInfo(lcPlayback).noquote() << "Source monitor opened" << path << "at" << frameRate.num << '/' << frameRate.den
                                 << "fps," << width << 'x' << height << "," << duration << "frames";
    return {};
}

void ClipPlayer::close()
{
    unload();
    m_path.clear();
}

}  // namespace mikserstudio::engine
