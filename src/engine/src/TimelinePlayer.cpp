// The program monitor's playback engine: following the project and rebuilding the timeline after changes.
#include "mikserstudio/engine/TimelinePlayer.h"

#include "Logging.h"
#include "TimelineBuilder.h"

#include <Mlt.h>

#include "mikserstudio/core/Project.h"

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

TimelinePlayer::TimelinePlayer(const MltRuntime& runtime, core::ProjectModel& project, AudioOutput audio,
                               QObject* parent)
    : MltPlayer(runtime, audio, parent)
    , m_project(project)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(kRebuildDelayMs);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &TimelinePlayer::rebuildNow);
    connect(&project, &core::ProjectModel::timelineChanged, this, &TimelinePlayer::scheduleRebuild);
    connect(&project, &core::ProjectModel::projectReset, this, &TimelinePlayer::scheduleRebuild);
    rebuildNow();
}

TimelinePlayer::~TimelinePlayer()
{
    unload();  // Stop the consumer before the cached producers go away.
}

void TimelinePlayer::scheduleRebuild()
{
    m_rebuildTimer.start();
}

void TimelinePlayer::rebuildNow()
{
    m_rebuildTimer.stop();
    const core::FramePos keepPosition = livePosition();
    const double keepSpeed = state() == State::Playing ? speed() : 0.0;

    // The builder's cached producers belong to the profile; drop them before it is replaced.
    const Format format{m_project.frameRate(), m_project.width(), m_project.height()};
    if (format != m_builtFormat)
        m_builder.reset();
    configureProfile(format.frameRate, format.width, format.height);
    if (!m_builder) {
        m_builder = std::make_unique<TimelineBuilder>(runtime(), profile());
        m_builtFormat = format;
    }
    BuiltTimeline built = m_builder->build(m_project);
    load(std::move(built.tractor), m_project.duration(), keepPosition, keepSpeed);

    if (!built.problems.isEmpty())
        Q_EMIT errorOccurred(built.problems.join(u'\n'));
    qCDebug(lcPlayback) << "Timeline rebuilt:" << m_project.clips().size() << "clips," << m_project.duration()
                        << "frames";
    Q_EMIT rebuilt();
}

}  // namespace mikserstudio::engine
