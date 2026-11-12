// The program monitor's playback engine: plays the timeline of the project and rebuilds itself when the project
// changes.
#pragma once

#include "mikserstudio/engine/MltPlayer.h"

#include <QTimer>

#include <memory>

namespace mikserstudio::core {
class ProjectModel;
}

namespace mikserstudio::engine {

class TimelineBuilder;

// Plays the project timeline (the program monitor). The timeline is an MLT tractor with a black
// background track and one playlist per project track: upper video tracks are composited over
// lower ones, the audio of every track is mixed. Hidden tracks show no video, muted tracks play
// no audio, audio tracks never show video.
//
// Changes to the project are coalesced (timelineChanged/projectReset) and the tractor is rebuilt
// shortly after, keeping the position and play state; while the format stays the same the output
// keeps running, so playback goes on without an audible restart. A new frame rate (project reset
// or load) emits frameRateChanged(). Main thread only.
class TimelinePlayer final : public MltPlayer {
    Q_OBJECT

public:
    static constexpr int kRebuildDelayMs = 40;

    TimelinePlayer(const MltRuntime& runtime, core::ProjectModel& project, AudioOutput audio = AudioOutput::Device,
                   QObject* parent = nullptr);
    ~TimelinePlayer() override;

    // Rebuilds right away instead of waiting for the debounce timer.
    void rebuildNow();

Q_SIGNALS:
    void rebuilt();  // After every rebuild, including the initial one.

private:
    struct Format {
        core::Rational frameRate;
        int width = 0;
        int height = 0;
        friend bool operator==(const Format&, const Format&) = default;
    };

    void scheduleRebuild();

    core::ProjectModel& m_project;
    std::unique_ptr<TimelineBuilder> m_builder;
    Format m_builtFormat;
    QTimer m_rebuildTimer;
};

}  // namespace mikserstudio::engine
