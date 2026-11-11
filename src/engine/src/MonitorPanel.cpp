#include "mikserstudio/engine/MonitorPanel.h"

#include "Shuttle.h"
#include "TransportBar.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/engine/MonitorWindow.h"

#include <QKeyEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace mikserstudio::engine {

MonitorPanel::MonitorPanel(core::Transport& transport, QWidget* parent)
    : QWidget(parent)
    , m_transport(transport)
    , m_window(new MonitorWindow)
    , m_bar(new TransportBar(transport, this))
{
    QWidget* container = QWidget::createWindowContainer(m_window, this);
    container->setMinimumSize(160, 90);
    container->setFocusPolicy(Qt::StrongFocus);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(container, 1);
    layout->addWidget(m_bar);

    connect(m_window, &MonitorWindow::keyPressed, this,
            [this](int key, Qt::KeyboardModifiers modifiers) { handleKey(key, modifiers); });
}

MonitorPanel::~MonitorPanel() = default;

std::shared_ptr<FrameSink> MonitorPanel::frameSink() const
{
    return m_window->frameSink();
}

void MonitorPanel::keyPressEvent(QKeyEvent* event)
{
    if (handleKey(event->key(), event->modifiers()))
        event->accept();
    else
        QWidget::keyPressEvent(event);
}

bool MonitorPanel::handleKey(int key, Qt::KeyboardModifiers modifiers)
{
    if ((modifiers & ~Qt::KeypadModifier) != Qt::NoModifier)
        return false;

    const bool playing = m_transport.state() == core::Transport::State::Playing;
    // The live speed: it may have been set elsewhere (the transport bar, the API).
    const auto shuttle = [this, playing](ShuttleKey shuttleKey) {
        m_transport.setSpeed(nextShuttleSpeed(playing ? m_transport.speed() : 0.0, shuttleKey));
    };
    const auto step = [this, playing](core::FramePos frames) {
        if (playing)
            m_transport.pause();
        const core::FramePos last = std::max<core::FramePos>(0, m_transport.duration() - 1);
        m_transport.seek(std::clamp<core::FramePos>(m_transport.position() + frames, 0, last));
    };

    switch (key) {
    case Qt::Key_Space:
        if (playing)
            m_transport.pause();
        else
            m_transport.play();
        return true;
    case Qt::Key_J:
        shuttle(ShuttleKey::Reverse);
        return true;
    case Qt::Key_K:
        shuttle(ShuttleKey::Stop);
        return true;
    case Qt::Key_L:
        shuttle(ShuttleKey::Forward);
        return true;
    case Qt::Key_Left:
        step(-1);
        return true;
    case Qt::Key_Right:
        step(1);
        return true;
    default:
        return false;
    }
}

}  // namespace mikserstudio::engine
