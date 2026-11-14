// The output consumer: audio outputs are tried device first and silent last, and a running output switches
// to another producer without stopping.
#include "EngineTestSupport.h"

#include "MltProfile.h"
#include "OutputConsumer.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <Mlt.h>

#include <atomic>

using namespace Qt::StringLiterals;
using mikserstudio::core::FramePos;
using mikserstudio::core::Rational;
using mikserstudio::engine::AudioOutput;
using mikserstudio::engine::makeProfile;
using mikserstudio::engine::OutputConsumer;
using mikserstudio::tests::waitUntil;

namespace {

// Sets an environment variable for the lifetime of the object.
class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : m_name(name)
        , m_previous(qgetenv(name))
        , m_wasSet(qEnvironmentVariableIsSet(name))
    {
        qputenv(name, value);
    }
    ~ScopedEnvironment()
    {
        if (m_wasSet)
            qputenv(m_name, m_previous);
        else
            qunsetenv(m_name);
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    const char* m_name;
    QByteArray m_previous;
    bool m_wasSet;
};

}  // namespace

TEST_CASE("Audio outputs are tried device first, silent last", "[output]")
{
    const auto& runtime = mikserstudio::tests::mltRuntime();
    CHECK(OutputConsumer::candidatesFor(AudioOutput::Silent, runtime) == QStringList{u"silent"_s});

    {
        const ScopedEnvironment unset("MIKSERSTUDIO_AUDIO_OUTPUT", {});
        const QStringList device = OutputConsumer::candidatesFor(AudioOutput::Device, runtime);
        REQUIRE_FALSE(device.isEmpty());
        CHECK(device.last() == u"silent"_s);
        if (runtime.hasConsumer(u"rtaudio"_s))
            CHECK(device.first() == u"rtaudio"_s);
    }
    {
        const ScopedEnvironment forced("MIKSERSTUDIO_AUDIO_OUTPUT", "silent");
        CHECK(OutputConsumer::candidatesFor(AudioOutput::Device, runtime) == QStringList{u"silent"_s});
    }
    {
        const ScopedEnvironment forced("MIKSERSTUDIO_AUDIO_OUTPUT", "sdl2_audio");
        if (runtime.hasConsumer(u"sdl2_audio"_s))
            CHECK(OutputConsumer::candidatesFor(AudioOutput::Device, runtime) ==
                  QStringList{u"sdl2_audio"_s, u"silent"_s});
    }
    {
        // An MLT consumer, but not an audio output: ignored.
        const ScopedEnvironment forced("MIKSERSTUDIO_AUDIO_OUTPUT", "null");
        CHECK_FALSE(OutputConsumer::candidatesFor(AudioOutput::Device, runtime).contains(u"null"_s));
    }
}

TEST_CASE("A running output switches to another producer without stopping", "[output]")
{
    const auto& runtime = mikserstudio::tests::mltRuntime();
    const auto profile = makeProfile(Rational{25, 1}, 64, 36);
    // Declared before the output: frames from both may be in flight until it stops.
    Mlt::Producer first(*profile, "color:#ff0000");
    Mlt::Producer second(*profile, "color:#0000ff");
    first.set_speed(1.0);
    second.set_speed(1.0);
    second.seek(500);

    std::atomic<FramePos> shown{-1};
    OutputConsumer output(*profile, Rational{25, 1}, OutputConsumer::candidatesFor(AudioOutput::Silent, runtime),
                          [&shown](mlt_frame frame) { shown.store(mlt_frame_get_position(frame)); });
    CHECK_FALSE(output.isRunning());
    CHECK_FALSE(output.replaceProducer(first));  // Nothing to switch over yet.

    REQUIRE(output.start(first).has_value());
    REQUIRE(output.isRunning());
    REQUIRE(waitUntil([&] { return shown.load() >= 2; }));

    REQUIRE(output.replaceProducer(second));
    CHECK(output.isRunning());
    CHECK(waitUntil([&] { return shown.load() >= 502; }));

    output.stop();
    CHECK_FALSE(output.isRunning());
}
