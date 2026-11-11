#include "mikserstudio/engine/MltPlayer.h"

#include "FrameConversion.h"
#include "Logging.h"
#include "MltProfile.h"
#include "OutputConsumer.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <Mlt.h>

#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace mikserstudio::engine {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr core::Rational kDefaultFrameRate{30, 1};
constexpr int kDefaultWidth = 1920;
constexpr int kDefaultHeight = 1080;

// After this long a request takes any frame, so a position MLT reports in an unexpected way can
// never freeze the playhead. Frames rendered before a request are shown well within it.
constexpr auto kSettleTimeout = 1s;

// How long a producer replaced on a running output is kept: frames rendered from it may still be
// on their way through the MLT threads.
constexpr auto kRetiredLifetime = 2s;

// The latest seek, speed change or load. Frames rendered before it can still reach the frame
// handler; they are told apart by position. Playback starts at `from` and moves in the playing
// direction no faster than real time, so a frame is taken only if it lies that way from `from`,
// within the distance playback can have covered since `at`, and not behind a frame already taken.
struct Request {
    core::FramePos from = 0;
    double speed = 0.0;
    double framesPerSecond = 0.0;
    Clock::time_point at;
    double travelled = 0.0;  // Of the last frame taken.

    bool take(core::FramePos shown, Clock::time_point now)
    {
        const auto elapsed = now - at;
        if (elapsed > kSettleTimeout)
            return true;
        if (speed == 0.0)
            return shown == from;
        const double distance = static_cast<double>(shown - from) * (speed > 0.0 ? 1.0 : -1.0);
        const double seconds = std::chrono::duration<double>(elapsed).count();
        const double reach = std::abs(speed) * (framesPerSecond * seconds + 2.0) + 2.0;
        if (distance < travelled || distance > reach)
            return false;
        travelled = distance;
        return true;
    }
};

}  // namespace

struct MltPlayer::Private {
    Private(const MltRuntime& mltRuntime, AudioOutput audioOutput)
        : runtime(mltRuntime)
        , audio(audioOutput)
    {
    }

    // Main thread: frames rendered before this call are dropped from now on.
    void expect(core::FramePos from, double requestedSpeed)
    {
        const std::scoped_lock lock(shownMutex);
        request = {.from = from,
                   .speed = requestedSpeed,
                   .framesPerSecond = frameRate.toDouble(),
                   .at = Clock::now(),
                   .travelled = 0.0};
        shownPosition = from;
    }

    // MLT thread: records `shown` unless it was rendered before the latest request.
    bool accept(core::FramePos shown)
    {
        const std::scoped_lock lock(shownMutex);
        if (!request.take(shown, Clock::now()))
            return false;
        shownPosition = shown;
        return true;
    }

    core::FramePos latestShown()
    {
        const std::scoped_lock lock(shownMutex);
        return shownPosition;
    }

    void retire(std::unique_ptr<Mlt::Producer> old)
    {
        const auto now = Clock::now();
        std::erase_if(retired, [now](const auto& entry) { return now - entry.first > kRetiredLifetime; });
        retired.emplace_back(now, std::move(old));
    }

    const MltRuntime& runtime;
    AudioOutput audio;

    core::Rational frameRate = kDefaultFrameRate;
    int width = kDefaultWidth;
    int height = kDefaultHeight;
    std::unique_ptr<Mlt::Profile> profile = makeProfile(kDefaultFrameRate, kDefaultWidth, kDefaultHeight);
    // Declared before the output so they outlive it; see kRetiredLifetime.
    std::vector<std::pair<Clock::time_point, std::unique_ptr<Mlt::Producer>>> retired;
    std::unique_ptr<OutputConsumer> output;  // Declared after the profile it refers to.
    std::unique_ptr<Mlt::Producer> producer;

    // Main-thread transport state.
    core::FramePos position = 0;
    core::FramePos duration = 0;
    State state = State::Stopped;
    double speed = 0.0;

    // Shared with the MLT thread.
    std::mutex sinkMutex;
    std::shared_ptr<FrameSink> sink;
    std::mutex shownMutex;  // Guards request and shownPosition.
    Request request;
    core::FramePos shownPosition = 0;
    std::atomic<bool> shownPending{false};
};

MltPlayer::MltPlayer(const MltRuntime& runtime, AudioOutput audio, QObject* parent)
    : core::Transport(parent)
    , d(std::make_unique<Private>(runtime, audio))
{
}

MltPlayer::~MltPlayer()
{
    d->output.reset();
    d->retired.clear();
    d->producer.reset();
}

core::FramePos MltPlayer::position() const
{
    return d->position;
}

core::FramePos MltPlayer::duration() const
{
    return d->duration;
}

core::Transport::State MltPlayer::state() const
{
    return d->state;
}

core::Rational MltPlayer::frameRate() const
{
    return d->frameRate;
}

double MltPlayer::speed() const
{
    return d->speed;
}

QString MltPlayer::audioOutputName() const
{
    return d->output ? d->output->name() : QString();
}

const MltRuntime& MltPlayer::runtime() const
{
    return d->runtime;
}

Mlt::Profile& MltPlayer::profile()
{
    return *d->profile;
}

core::FramePos MltPlayer::livePosition() const
{
    return d->state == State::Playing ? d->latestShown() : d->position;
}

void MltPlayer::setFrameSink(std::shared_ptr<FrameSink> sink)
{
    const std::scoped_lock lock(d->sinkMutex);
    d->sink = std::move(sink);
}

void MltPlayer::play()
{
    setSpeed(1.0);
}

void MltPlayer::pause()
{
    if (d->state != State::Stopped)
        applySpeed(0.0);
}

void MltPlayer::setSpeed(double speed)
{
    if (d->state != State::Stopped && std::isfinite(speed))
        applySpeed(speed);
}

void MltPlayer::seek(core::FramePos frame)
{
    if (d->state == State::Stopped || !d->producer)
        return;
    const core::FramePos target = clampPosition(frame);
    d->producer->seek(static_cast<int>(target));
    d->expect(target, d->speed);
    setPosition(target);
    d->output->purge();
    d->output->refresh();
}

bool MltPlayer::configureProfile(core::Rational frameRate, int width, int height)
{
    if (profileMatches(*d->profile, frameRate, width, height))
        return false;
    // The consumer and the producers were made for the old profile.
    d->output.reset();
    d->retired.clear();
    d->producer.reset();
    d->profile = makeProfile(frameRate, width, height);
    const bool rateChanged = d->frameRate != frameRate;
    d->frameRate = frameRate;
    d->width = width;
    d->height = height;
    d->speed = 0.0;
    setState(State::Stopped);
    if (rateChanged)
        Q_EMIT frameRateChanged();
    return true;
}

void MltPlayer::load(std::unique_ptr<Mlt::Producer> producer, core::FramePos duration, core::FramePos position,
                     double speed)
{
    if (!d->output) {
        d->output = std::make_unique<OutputConsumer>(
            *d->profile, d->frameRate, OutputConsumer::candidatesFor(d->audio, d->runtime),
            [this](mlt_frame frame) {
                const core::FramePos shown = mlt_frame_get_position(frame);
                if (!d->accept(shown))
                    return;
                std::shared_ptr<FrameSink> sink;
                {
                    const std::scoped_lock lock(d->sinkMutex);
                    sink = d->sink;
                }
                if (sink) {
                    QImage image = frameToImage(frame, d->width, d->height);
                    if (!image.isNull())
                        sink->presentFrame(VideoFrame{.image = std::move(image), .position = shown});
                }
                if (!d->shownPending.exchange(true))
                    QMetaObject::invokeMethod(this, [this] { onFrameShown(); }, Qt::QueuedConnection);
            });
    }
    std::unique_ptr<Mlt::Producer> previous = std::move(d->producer);
    d->producer = std::move(producer);
    setDuration(duration);

    const core::FramePos start = clampPosition(position);
    d->producer->set_speed(speed);
    d->producer->seek(static_cast<int>(start));
    d->expect(start, speed);
    setPosition(start);

    if (previous && d->output->isRunning() && d->output->replaceProducer(*d->producer)) {
        d->retire(std::move(previous));
    } else {
        d->output->stop();
        d->retired.clear();
        previous.reset();
        if (auto started = d->output->start(*d->producer); !started) {
            d->producer.reset();
            d->speed = 0.0;
            setState(State::Stopped);
            Q_EMIT errorOccurred(started.error());
            return;
        }
    }
    d->speed = speed;
    setState(speed != 0.0 ? State::Playing : State::Paused);
    d->output->refresh();
}

void MltPlayer::unload()
{
    if (d->output)
        d->output->stop();
    d->retired.clear();
    d->producer.reset();
    d->speed = 0.0;
    setDuration(0);
    setPosition(0);
    setState(State::Stopped);
}

void MltPlayer::applySpeed(double speed)
{
    if (!d->producer)
        return;
    if (speed == 0.0) {
        d->producer->set_speed(0.0);
        d->producer->seek(static_cast<int>(d->position));  // Stay on the frame on screen.
        d->speed = 0.0;
        d->expect(d->position, 0.0);
        d->output->purge();
        d->output->refresh();
        setState(State::Paused);
        return;
    }
    if (d->duration <= 0)
        return;

    const core::FramePos last = d->duration - 1;
    if (speed > 0.0 && d->position >= last)
        setPosition(0);
    else if (speed < 0.0 && d->position <= 0)
        setPosition(last);
    d->producer->set_speed(speed);
    d->producer->seek(static_cast<int>(d->position));
    d->speed = speed;
    d->expect(d->position, speed);
    d->output->purge();
    d->output->refresh();
    setState(State::Playing);
}

void MltPlayer::onFrameShown()
{
    d->shownPending.store(false);
    if (d->state != State::Playing)
        return;

    // Frames from before the latest request never get here (see Private::accept).
    const core::FramePos shown = d->latestShown();
    setPosition(clampPosition(shown));
    const core::FramePos last = std::max<core::FramePos>(0, d->duration - 1);
    if ((d->speed > 0.0 && shown >= last) || (d->speed < 0.0 && shown <= 0))
        applySpeed(0.0);
}

core::FramePos MltPlayer::clampPosition(core::FramePos frame) const
{
    return std::clamp<core::FramePos>(frame, 0, std::max<core::FramePos>(0, d->duration - 1));
}

void MltPlayer::setPosition(core::FramePos position)
{
    if (d->position == position)
        return;
    d->position = position;
    Q_EMIT positionChanged(position);
}

void MltPlayer::setDuration(core::FramePos duration)
{
    if (d->duration == duration)
        return;
    d->duration = duration;
    Q_EMIT durationChanged(duration);
}

void MltPlayer::setState(State state)
{
    if (d->state == state)
        return;
    d->state = state;
    Q_EMIT stateChanged(state);
}

}  // namespace mikserstudio::engine
