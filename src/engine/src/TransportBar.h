#pragma once

#include "mikserstudio/core/Transport.h"

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace mikserstudio::engine {

// Play/pause button, timecode ("position / duration") and a seek slider for one transport.
// Dragging the slider scrubs (seeks while paused show the frame). The controls take no keyboard
// focus, so the monitor panel keeps its J/K/L/Space keys.
class TransportBar final : public QWidget {
    Q_OBJECT

public:
    explicit TransportBar(core::Transport& transport, QWidget* parent = nullptr);

private:
    void updatePosition(core::FramePos position);
    void updateDuration(core::FramePos duration);
    void updateState(core::Transport::State state);
    void updateTimecode();

    core::Transport& m_transport;
    QToolButton* m_playButton = nullptr;
    QLabel* m_timecode = nullptr;
    QSlider* m_slider = nullptr;
};

}  // namespace mikserstudio::engine
