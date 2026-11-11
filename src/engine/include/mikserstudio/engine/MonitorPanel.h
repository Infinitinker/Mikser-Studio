#pragma once

#include "mikserstudio/engine/FrameSink.h"

#include <QWidget>

#include <memory>

namespace mikserstudio::core {
class Transport;
}

namespace mikserstudio::engine {

class MonitorWindow;
class TransportBar;

// A monitor panel: the GPU video surface plus a compact transport bar (play/pause, timecode,
// seek slider). Keys while the panel or its video has focus: Space play/pause, J/K/L shuttle
// (repeat J or L to go faster), Left/Right step one frame.
//
// Wire it as player.setFrameSink(panel.frameSink()). The transport must outlive the panel.
class MonitorPanel final : public QWidget {
    Q_OBJECT

public:
    explicit MonitorPanel(core::Transport& transport, QWidget* parent = nullptr);
    ~MonitorPanel() override;

    [[nodiscard]] std::shared_ptr<FrameSink> frameSink() const;
    [[nodiscard]] MonitorWindow* monitorWindow() const { return m_window; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool handleKey(int key, Qt::KeyboardModifiers modifiers);

    core::Transport& m_transport;
    MonitorWindow* m_window = nullptr;  // Owned by its container widget.
    TransportBar* m_bar = nullptr;
};

}  // namespace mikserstudio::engine
