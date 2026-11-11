#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace mikserstudio::core {
class ProjectModel;
class Transport;
}  // namespace mikserstudio::core

namespace mikserstudio::ui::demo {

class MonitorCanvas;

// A stand-in monitor panel: a 16:9 "picture" naming the clip under the playhead (no decoding),
// the transport's timecode and a play/pause button.
class DemoMonitorPanel final : public QWidget {
    Q_OBJECT

public:
    DemoMonitorPanel(const QString& title, core::Transport& transport, const core::ProjectModel& project,
                     QWidget* parent = nullptr);

private:
    void updateTransport();

    core::Transport& m_transport;
    MonitorCanvas* m_canvas;
    QLabel* m_timecode;
    QLabel* m_duration;
    QToolButton* m_play;
};

}  // namespace mikserstudio::ui::demo
