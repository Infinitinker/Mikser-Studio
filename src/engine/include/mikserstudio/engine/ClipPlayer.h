#pragma once

#include "mikserstudio/engine/MltPlayer.h"

#include <QString>

#include <expected>

namespace mikserstudio::engine {

// Plays one media file (the source monitor). The output takes the file's own frame rate and
// size; stills, colours and audio-only files play at 1920x1080, 30 fps. Main thread only.
class ClipPlayer final : public MltPlayer {
    Q_OBJECT

public:
    static constexpr core::Rational kFallbackFrameRate{30, 1};
    static constexpr int kFallbackWidth = 1920;
    static constexpr int kFallbackHeight = 1080;

    explicit ClipPlayer(const MltRuntime& runtime, AudioOutput audio = AudioOutput::Device,
                        QObject* parent = nullptr);
    ~ClipPlayer() override;

    // Opens an absolute local file or a colour resource ("color:#ff0000") paused on its first
    // frame; emits frameRateChanged() when the file has another rate. A missing, refused or
    // unreadable file is reported before anything changes, so the current file stays loaded.
    std::expected<void, QString> openFile(const QString& path);
    void close();

    [[nodiscard]] QString filePath() const { return m_path; }

private:
    QString m_path;
};

}  // namespace mikserstudio::engine
