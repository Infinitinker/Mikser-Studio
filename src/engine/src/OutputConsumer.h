// The MLT consumer that plays a producer in real time (audio device, else silent) and reports each shown frame.
#pragma once

#include "SilentPacer.h"
#include "mikserstudio/engine/MltPlayer.h"

#include <framework/mlt.h>  // Declares the C API with C linkage.

#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <memory>

namespace Mlt {
class Consumer;
class Event;
class Producer;
class Profile;
}  // namespace Mlt

namespace mikserstudio::engine {

class MltRuntime;

// The MLT consumer that plays a producer in real time and reports shown frames.
//
// AudioOutput::Device tries "rtaudio" (the audio device clock drives video), then "sdl2_audio",
// then the silent output; a consumer that cannot be created or started is dropped for good and
// the next one is tried. The silent output is MLT's "null" consumer paced by a SilentPacer.
//
// The handler runs on an MLT thread for every shown frame.
class OutputConsumer {
public:
    using FrameHandler = std::function<void(mlt_frame)>;

    OutputConsumer(Mlt::Profile& profile, core::Rational frameRate, QStringList candidates, FrameHandler handler);
    ~OutputConsumer();
    OutputConsumer(const OutputConsumer&) = delete;
    OutputConsumer& operator=(const OutputConsumer&) = delete;

    // The consumer services to try for an audio mode, in order ("silent" = paced null consumer).
    // For AudioOutput::Device, $MIKSERSTUDIO_AUDIO_OUTPUT ("rtaudio", "sdl2_audio" or "silent") forces
    // one output (still falling back to silent if it fails).
    [[nodiscard]] static QStringList candidatesFor(AudioOutput audio, const MltRuntime& runtime);

    std::expected<void, QString> start(Mlt::Producer& producer);
    // Switches a running consumer over to `producer` without stopping it (the audio device stays
    // open) and drops the frames rendered from the old one. The caller keeps the old producer
    // alive for a while: frames from it may still be on their way. False if MLT refused.
    bool replaceProducer(Mlt::Producer& producer);
    void stop();
    [[nodiscard]] bool isRunning() const;
    void refresh();  // Show the producer's current frame again (needed while paused).
    void purge();  // Drop frames rendered ahead of time.
    [[nodiscard]] QString name() const { return m_name; }

private:
    bool startCandidate(const QString& candidate, Mlt::Producer& producer);
    bool create(const QString& candidate);
    void release();
    void showFrame(mlt_frame frame);
    static void onFrameShow(mlt_properties owner, void* self, mlt_event_data data);

    Mlt::Profile& m_profile;
    QStringList m_candidates;
    FrameHandler m_handler;
    SilentPacer m_pacer;
    std::unique_ptr<Mlt::Consumer> m_consumer;
    std::unique_ptr<Mlt::Event> m_listener;
    QString m_name;
    bool m_paced = false;
};

}  // namespace mikserstudio::engine
