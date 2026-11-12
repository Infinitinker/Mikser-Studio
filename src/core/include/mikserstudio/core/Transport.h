#pragma once

#include "mikserstudio/core/Project.h"

#include <QObject>

namespace mikserstudio::core {

class CommandBus;

// Playback control of one monitor (source or program). Implemented by the engine; the UI and
// commands only see this interface. All methods are called on the main thread; signals are
// emitted on the main thread.
class Transport : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum class State { Stopped, Paused, Playing };
    Q_ENUM(State)

    using QObject::QObject;
    ~Transport() override = default;

    [[nodiscard]] virtual FramePos position() const = 0;
    [[nodiscard]] virtual FramePos duration() const = 0;
    [[nodiscard]] virtual State state() const = 0;
    [[nodiscard]] virtual Rational frameRate() const = 0;
    [[nodiscard]] virtual double speed() const = 0;  // Like setSpeed(); 0 while paused or stopped.

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(FramePos frame) = 0;  // Clamped to [0, duration).
    virtual void setSpeed(double speed) = 0;  // 1 = normal, negative = reverse, 0 = pause (J/K/L).

Q_SIGNALS:
    void positionChanged(qint64 frame);
    void durationChanged(qint64 frames);
    void stateChanged(mikserstudio::core::Transport::State state);
    void frameRateChanged();  // E.g. when a source clip with another frame rate is opened.
};

// Registers "<prefix>.play", ".pause", ".toggle", ".seek" {frame}, ".step" {frames},
// ".shuttle" {speed} and ".status" (readOnly) for one transport, e.g. prefix "program".
// Every one returns the status {position, duration, state: "stopped"|"paused"|"playing",
// frameRate: {num, den}} after it ran. frame is 0..kMaxFrame, frames -kMaxFrame..kMaxFrame.
// ".step" defaults to 1 frame, pauses and clamps to [0, duration - 1]. No default shortcuts: the
// UI binds J/K/L/Space to the focused monitor.
void registerTransportCommands(CommandBus& bus, Transport& transport, const QString& prefix,
                               const QString& displayName);

}  // namespace mikserstudio::core
