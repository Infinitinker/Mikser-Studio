// The monitor panel: its keys drive the transport with J/K/L shuttle, the transport bar shows the timecode
// and scrubs, and a docked monitor keeps presenting after it is floated and docked again.
#include "EngineTestSupport.h"

#include "mikserstudio/core/Transport.h"
#include "mikserstudio/engine/MonitorPanel.h"
#include "mikserstudio/engine/MonitorWindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QSlider>

#include <algorithm>
#include <vector>

using namespace Qt::StringLiterals;
using mikserstudio::core::FramePos;
using mikserstudio::core::Rational;
using mikserstudio::core::Transport;
using mikserstudio::engine::MonitorPanel;
using mikserstudio::tests::waitUntil;

namespace {

// Records what the panel asks for and behaves like a simple player.
class FakeTransport final : public Transport {
public:
    FramePos position() const override { return m_position; }
    FramePos duration() const override { return 100; }
    State state() const override { return m_state; }
    Rational frameRate() const override { return {25, 1}; }
    double speed() const override { return m_speed; }

    void play() override { setSpeed(1.0); }
    void pause() override { setSpeed(0.0); }
    void seek(FramePos frame) override
    {
        seeks.push_back(frame);
        m_position = std::clamp<FramePos>(frame, 0, 99);
        Q_EMIT positionChanged(m_position);
    }
    void setSpeed(double speed) override
    {
        speeds.push_back(speed);
        m_speed = speed;
        const State next = speed == 0.0 ? State::Paused : State::Playing;
        if (next != m_state) {
            m_state = next;
            Q_EMIT stateChanged(m_state);
        }
    }

    std::vector<double> speeds;
    std::vector<FramePos> seeks;

private:
    FramePos m_position = 0;
    double m_speed = 0.0;
    State m_state = State::Paused;
};

void press(QWidget& widget, int key)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

}  // namespace

TEST_CASE("Monitor panel keys drive the transport with J/K/L shuttle", "[panel]")
{
    FakeTransport transport;
    MonitorPanel panel(transport);

    press(panel, Qt::Key_L);
    press(panel, Qt::Key_L);
    press(panel, Qt::Key_L);
    press(panel, Qt::Key_K);
    press(panel, Qt::Key_J);
    press(panel, Qt::Key_J);
    press(panel, Qt::Key_Space);
    press(panel, Qt::Key_Space);
    CHECK(transport.speeds == std::vector<double>{1.0, 2.0, 4.0, 0.0, -1.0, -2.0, 0.0, 1.0});

    press(panel, Qt::Key_Right);  // Pauses, then steps.
    CHECK(transport.state() == Transport::State::Paused);
    press(panel, Qt::Key_Right);
    press(panel, Qt::Key_Left);
    press(panel, Qt::Key_Left);
    press(panel, Qt::Key_Left);
    CHECK(transport.seeks == std::vector<FramePos>{1, 2, 1, 0, 0});

    // Keys pressed on the video surface reach the panel as well.
    Q_EMIT panel.monitorWindow()->keyPressed(Qt::Key_L, Qt::NoModifier);
    CHECK(transport.speeds.back() == 1.0);
    // Shortcuts with modifiers are left to the application.
    QKeyEvent ctrlL(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QApplication::sendEvent(&panel, &ctrlL);
    CHECK_FALSE(ctrlL.isAccepted());
}

TEST_CASE("J/K/L shuttle starts from the speed set elsewhere", "[panel]")
{
    FakeTransport transport;
    MonitorPanel panel(transport);

    transport.setSpeed(4.0);  // E.g. program.shuttle {speed: 4} from the API.
    press(panel, Qt::Key_L);
    CHECK(transport.speeds.back() == 8.0);

    transport.setSpeed(-2.0);  // Changed while playing: no stateChanged.
    press(panel, Qt::Key_J);
    CHECK(transport.speeds.back() == -4.0);
    press(panel, Qt::Key_L);
    CHECK(transport.speeds.back() == 1.0);
}

TEST_CASE("The transport bar shows the timecode and scrubs with its slider", "[panel]")
{
    FakeTransport transport;
    MonitorPanel panel(transport);
    const auto labels = panel.findChildren<QLabel*>();
    const auto timecode = std::ranges::find_if(labels, [](QLabel* label) { return label->text().contains(u'/'); });
    REQUIRE(timecode != labels.end());
    CHECK((*timecode)->text() == u"00:00:00:00 / 00:00:04:00"_s);

    transport.seek(51);
    CHECK((*timecode)->text() == u"00:00:02:01 / 00:00:04:00"_s);

    auto* slider = panel.findChild<QSlider*>();
    REQUIRE(slider != nullptr);
    CHECK(slider->maximum() == 99);
    CHECK(slider->value() == 51);
    transport.seeks.clear();
    slider->triggerAction(QAbstractSlider::SliderPageStepAdd);
    CHECK(transport.seeks == std::vector<FramePos>{76});
}

TEST_CASE("A docked monitor keeps presenting after being floated and docked again", "[panel][monitor]")
{
    FakeTransport transport;
    QMainWindow window;
    auto* dock = new QDockWidget(u"Monitor"_s, &window);
    auto* panel = new MonitorPanel(transport, dock);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    window.resize(640, 400);
    window.show();

    mikserstudio::engine::MonitorWindow* monitor = panel->monitorWindow();
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    const auto sink = panel->frameSink();

    QImage image(64, 36, QImage::Format_RGBA8888);
    image.fill(Qt::cyan);
    const auto presentAndWait = [&](FramePos position) {
        const quint64 before = monitor->framesPresented();
        sink->presentFrame({.image = image, .position = position});
        return waitUntil([&] { return monitor->framesPresented() > before; });
    };

    REQUIRE(presentAndWait(1));
    dock->setFloating(true);
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    CHECK(presentAndWait(2));
    dock->setFloating(false);
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    CHECK(presentAndWait(3));
    CHECK(monitor->grabFramebuffer().has_value());
}
