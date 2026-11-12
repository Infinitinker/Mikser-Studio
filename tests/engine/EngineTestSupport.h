// Helpers the engine tests share: Catch2 printers for Qt types, the MLT runtime, a frame sink that records
// frames, and waiting for a condition while the event loop runs.
#pragma once

#include "mikserstudio/engine/FrameSink.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <QColor>
#include <QSize>
#include <QString>

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

template <> struct Catch::StringMaker<QString> {
    static std::string convert(const QString& value) { return '"' + value.toStdString() + '"'; }
};

template <> struct Catch::StringMaker<QColor> {
    static std::string convert(const QColor& value) { return value.name(QColor::HexArgb).toStdString(); }
};

template <> struct Catch::StringMaker<QSize> {
    static std::string convert(const QSize& value)
    {
        return std::to_string(value.width()) + 'x' + std::to_string(value.height());
    }
};

namespace mikserstudio::engine {
class MltRuntime;
}

namespace mikserstudio::tests {

using namespace std::chrono_literals;

// The initialized MLT runtime; fails the test when MLT cannot be started.
const engine::MltRuntime& mltRuntime();

// Processes Qt events until `done` returns true or `timeout` passes. Returns `done()`.
bool waitUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5s);

// Path of the sample video, or nothing when it is not on this machine.
std::optional<QString> sampleVideo();

// Color of the center pixel.
QColor centerColor(const QImage& image);

// True when every channel differs by at most `tolerance` (compositing rounds by a step or two).
bool closeTo(const QColor& actual, const QColor& expected, int tolerance = 3);

// Keeps the most recent presented frames (and counts all of them); thread-safe.
class RecordingSink final : public engine::FrameSink {
public:
    void presentFrame(engine::VideoFrame frame) override;

    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] std::optional<engine::VideoFrame> last() const;
    // The most recent frame shown at `position`, if any arrived.
    [[nodiscard]] std::optional<engine::VideoFrame> lastAt(core::FramePos position) const;
    void clear();

private:
    static constexpr std::size_t kKept = 4;

    mutable std::mutex m_mutex;
    std::deque<engine::VideoFrame> m_frames;
    std::size_t m_count = 0;
};

// Waits for a frame at `position` to reach `sink`.
std::optional<engine::VideoFrame> waitForFrameAt(const RecordingSink& sink, core::FramePos position,
                                                 std::chrono::milliseconds timeout = 5s);

}  // namespace mikserstudio::tests
