#pragma once

#include <QObject>
#include <QString>

#include <chrono>

namespace mikserstudio::engine {
class ClipPlayer;
}

namespace mikserstudio::demo {

class DemoWindow;

// Plays for `duration`, floats the monitor dock at a third of it and docks it again at two
// thirds, then reads the rendered monitor back to `outputPath`, prints the statistics and quits
// the application with 0 on success or 1 when no frames were shown, the monitor stopped showing
// frames after re-docking, or the read-back picture is empty or uniform.
class SelfTest final : public QObject {
    Q_OBJECT

public:
    SelfTest(engine::ClipPlayer& player, DemoWindow& window, std::chrono::milliseconds duration, QString outputPath);

    void start();

private:
    void floatDock();
    void redock();
    void finish();
    [[nodiscard]] QString checkImage(const QImage& image) const;

    engine::ClipPlayer& m_player;
    DemoWindow& m_window;
    std::chrono::milliseconds m_duration;
    QString m_outputPath;
    quint64 m_presentedBeforeFloat = 0;
    quint64 m_presentedAtRedock = 0;
};

}  // namespace mikserstudio::demo
