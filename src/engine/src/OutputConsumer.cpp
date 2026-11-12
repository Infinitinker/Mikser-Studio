#include "OutputConsumer.h"

#include "Logging.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <Mlt.h>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

const QString kSilent = u"silent"_s;

}  // namespace

OutputConsumer::OutputConsumer(Mlt::Profile& profile, core::Rational frameRate, QStringList candidates,
                               FrameHandler handler)
    : m_profile(profile)
    , m_candidates(std::move(candidates))
    , m_handler(std::move(handler))
    , m_pacer(frameRate)
{
}

OutputConsumer::~OutputConsumer()
{
    release();
}

QStringList OutputConsumer::candidatesFor(AudioOutput audio, const MltRuntime& runtime)
{
    if (audio == AudioOutput::Silent)
        return {kSilent};

    const QStringList devices{u"rtaudio"_s, u"sdl2_audio"_s};
    const QString forced = qEnvironmentVariable("MIKSERSTUDIO_AUDIO_OUTPUT");
    if (forced == kSilent)
        return {kSilent};
    if (devices.contains(forced) && runtime.hasConsumer(forced))
        return {forced, kSilent};
    if (!forced.isEmpty())
        qCWarning(lcPlayback).noquote() << u"Ignoring MIKSERSTUDIO_AUDIO_OUTPUT="_s + forced +
                                               u"; use rtaudio, sdl2_audio or silent (if this MLT has them)"_s;

    QStringList candidates;
    for (const QString& service : devices) {
        if (runtime.hasConsumer(service))
            candidates.append(service);
    }
    candidates.append(kSilent);
    return candidates;
}

std::expected<void, QString> OutputConsumer::start(Mlt::Producer& producer)
{
    while (!m_candidates.isEmpty()) {
        const QString candidate = m_candidates.constFirst();
        if (startCandidate(candidate, producer)) {
            if (m_name != candidate)
                qCInfo(lcPlayback).noquote() << "Playing through MLT consumer" << candidate;
            m_name = candidate;
            return {};
        }
        qCWarning(lcPlayback).noquote() << "MLT consumer" << candidate << "could not be started; trying the next one";
        release();
        m_candidates.removeFirst();
    }
    return std::unexpected(u"Playback could not start: no MLT output (audio device or silent) works on this system."_s);
}

bool OutputConsumer::startCandidate(const QString& candidate, Mlt::Producer& producer)
{
    if (!m_consumer && !create(candidate))
        return false;
    m_pacer.reset();
    if (m_consumer->connect(producer) != 0)
        return false;
    return m_consumer->start() == 0 && !m_consumer->is_stopped();
}

bool OutputConsumer::create(const QString& candidate)
{
    m_paced = candidate == kSilent;
    const QByteArray service = m_paced ? QByteArrayLiteral("null") : candidate.toUtf8();
    m_consumer = std::make_unique<Mlt::Consumer>(m_profile, service.constData());
    if (!m_consumer->is_valid()) {
        m_consumer.reset();
        return false;
    }

    m_consumer->set("mlt_image_format", "rgba");
    m_consumer->set("rescale", "bilinear");
    m_consumer->set("terminate_on_pause", 0);
    if (m_paced) {
        // Render in the consumer thread, inside the frame-show handler, paced by SilentPacer.
        m_consumer->set("real_time", 0);
        m_consumer->set("audio_off", 1);
    } else {
        m_consumer->set("real_time", 1);
        m_consumer->set("frequency", 48000);
        m_consumer->set("channels", 2);
        m_consumer->set("scrub_audio", 1);
        m_consumer->set("volume", 1.0);
    }
    m_listener.reset(m_consumer->listen("consumer-frame-show", this, onFrameShow));
    return true;
}

bool OutputConsumer::replaceProducer(Mlt::Producer& producer)
{
    if (!isRunning())
        return false;
    // The consumer thread fetches frames from its input under the service lock.
    m_consumer->lock();
    const bool connected = m_consumer->connect(producer) == 0;
    m_consumer->unlock();
    if (connected)
        m_consumer->purge();
    return connected;
}

bool OutputConsumer::isRunning() const
{
    return m_consumer && !m_consumer->is_stopped();
}

void OutputConsumer::stop()
{
    if (!m_consumer)
        return;
    m_pacer.cancel();
    m_consumer->stop();
}

void OutputConsumer::release()
{
    stop();
    m_listener.reset();
    m_consumer.reset();
}

void OutputConsumer::refresh()
{
    if (!m_consumer)
        return;
    if (m_paced)
        m_pacer.refresh();
    else
        m_consumer->set("refresh", 1);
}

void OutputConsumer::purge()
{
    if (m_consumer)
        m_consumer->purge();
}

void OutputConsumer::onFrameShow(mlt_properties, void* self, mlt_event_data data)
{
    if (mlt_frame frame = mlt_event_data_to_frame(data))
        static_cast<OutputConsumer*>(self)->showFrame(frame);
}

void OutputConsumer::showFrame(mlt_frame frame)
{
    if (!m_paced) {
        m_handler(frame);
        return;
    }
    const double speed = mlt_properties_get_double(MLT_FRAME_PROPERTIES(frame), "_speed");
    if (speed != 0.0 && !m_pacer.waitUntilDue(speed))
        return;
    m_handler(frame);
    if (speed == 0.0)
        m_pacer.waitForRefresh();
}

}  // namespace mikserstudio::engine
