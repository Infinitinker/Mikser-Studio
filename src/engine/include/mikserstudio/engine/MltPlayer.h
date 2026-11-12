// The playback engine under both monitors: one MLT producer played through one MLT consumer, with the audio device
// as the master clock. ClipPlayer and TimelinePlayer build on it.
#pragma once

#include "mikserstudio/core/Transport.h"
#include "mikserstudio/engine/FrameSink.h"

#include <memory>

namespace Mlt {
class Producer;
class Profile;
}  // namespace Mlt

namespace mikserstudio::engine {

class MltRuntime;

// Where a player sends its audio. With Device, $MIKSERSTUDIO_AUDIO_OUTPUT=rtaudio|sdl2_audio|silent
// forces one output (useful for headless runs and diagnosing audio problems).
enum class AudioOutput {
    Device,  // The default audio device ("rtaudio", else "sdl2_audio"); silent if neither works.
    Silent,  // No audio; frames are paced by a steady clock. Used by tests and headless runs.
};

// The playback engine shared by ClipPlayer and TimelinePlayer: one MLT producer played through
// one MLT consumer. With an audio device the device clock is the master clock and frames are
// shown when the consumer's "consumer-frame-show" event fires.
//
// Frames go straight from the MLT thread to the FrameSink. Positions reach the main thread
// through queued, coalesced calls (at most one pending at a time). After a seek, speed change or
// load, frames rendered before it are dropped on the MLT thread (neither shown nor reported).
// The consumer keeps running while the profile stays the same, so replacing what is played does
// not reopen the audio device. All public methods are main-thread only.
class MltPlayer : public core::Transport {
    Q_OBJECT

public:
    ~MltPlayer() override;

    [[nodiscard]] core::FramePos position() const override;
    [[nodiscard]] core::FramePos duration() const override;
    [[nodiscard]] State state() const override;
    [[nodiscard]] core::Rational frameRate() const override;
    [[nodiscard]] double speed() const override;

    void play() override;
    void pause() override;
    void seek(core::FramePos frame) override;
    void setSpeed(double speed) override;

    // The sink receives every shown frame; nullptr detaches it. The player keeps it alive.
    void setFrameSink(std::shared_ptr<FrameSink> sink);

    // The MLT consumer in use: "rtaudio", "sdl2_audio" or "silent"; empty before anything loads.
    [[nodiscard]] QString audioOutputName() const;

Q_SIGNALS:
    // Something could not be played (missing media, no usable consumer). English, user-facing.
    void errorOccurred(const QString& message);

protected:
    MltPlayer(const MltRuntime& runtime, AudioOutput audio, QObject* parent);

    [[nodiscard]] const MltRuntime& runtime() const;

    // Makes the output format fps/width/height. Returns true when it changed, which stops
    // playback and drops the current producer: producers made for the old profile are invalid.
    // Emits frameRateChanged() when the rate changed.
    bool configureProfile(core::Rational frameRate, int width, int height);
    [[nodiscard]] Mlt::Profile& profile();

    // Replaces what is played. Positions the producer at `position` (clamped), starts the
    // consumer (a running one is switched over without stopping) and plays at `speed`
    // (0 = paused, showing that frame). `duration` is what Transport reports; it may be shorter
    // than the producer (e.g. an empty timeline).
    void load(std::unique_ptr<Mlt::Producer> producer, core::FramePos duration, core::FramePos position, double speed);
    void unload();  // Stops playback; state becomes Stopped.

    // The frame on screen. While playing this is the newest shown frame, which can be ahead of
    // position() (that one reaches the main thread through the event queue).
    [[nodiscard]] core::FramePos livePosition() const;

private:
    struct Private;

    void onFrameShown();
    void setPosition(core::FramePos position);
    void setDuration(core::FramePos duration);
    void setState(State state);
    void applySpeed(double speed);
    [[nodiscard]] core::FramePos clampPosition(core::FramePos frame) const;

    std::unique_ptr<Private> d;
};

}  // namespace mikserstudio::engine
