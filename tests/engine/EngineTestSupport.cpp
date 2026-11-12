// Helpers the engine tests share: the MLT runtime, waiting for a condition while the event loop runs, and
// the frame sink that records what it receives.
#include "EngineTestSupport.h"

#include "mikserstudio/engine/MltRuntime.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>

#include <cstdlib>
#include <ranges>

namespace mikserstudio::tests {

const engine::MltRuntime& mltRuntime()
{
    const auto runtime = engine::MltRuntime::initialize();
    if (!runtime)
        FAIL("MLT did not start: " << runtime.error().toStdString());
    return **runtime;
}

bool waitUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return true;
}

std::optional<QString> sampleVideo()
{
    QString path = QStringLiteral(MIKSERSTUDIO_SAMPLE_VIDEO);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return std::nullopt;
    return path;
}

QColor centerColor(const QImage& image)
{
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

bool closeTo(const QColor& actual, const QColor& expected, int tolerance)
{
    return std::abs(actual.red() - expected.red()) <= tolerance &&
           std::abs(actual.green() - expected.green()) <= tolerance &&
           std::abs(actual.blue() - expected.blue()) <= tolerance &&
           std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

void RecordingSink::presentFrame(engine::VideoFrame frame)
{
    const std::scoped_lock lock(m_mutex);
    m_frames.push_back(std::move(frame));
    if (m_frames.size() > kKept)
        m_frames.pop_front();
    ++m_count;
}

std::size_t RecordingSink::count() const
{
    const std::scoped_lock lock(m_mutex);
    return m_count;
}

std::optional<engine::VideoFrame> RecordingSink::last() const
{
    const std::scoped_lock lock(m_mutex);
    if (m_frames.empty())
        return std::nullopt;
    return m_frames.back();
}

std::optional<engine::VideoFrame> RecordingSink::lastAt(core::FramePos position) const
{
    const std::scoped_lock lock(m_mutex);
    for (const auto& frame : std::views::reverse(m_frames)) {
        if (frame.position == position)
            return frame;
    }
    return std::nullopt;
}

void RecordingSink::clear()
{
    const std::scoped_lock lock(m_mutex);
    m_frames.clear();
    m_count = 0;
}

std::optional<engine::VideoFrame> waitForFrameAt(const RecordingSink& sink, core::FramePos position,
                                                 std::chrono::milliseconds timeout)
{
    std::optional<engine::VideoFrame> frame;
    waitUntil(
        [&] {
            frame = sink.lastAt(position);
            return frame.has_value();
        },
        timeout);
    return frame;
}

}  // namespace mikserstudio::tests
