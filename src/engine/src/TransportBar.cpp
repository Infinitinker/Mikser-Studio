#include "TransportBar.h"

#include "mikserstudio/engine/Timecode.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

TransportBar::TransportBar(core::Transport& transport, QWidget* parent)
    : QWidget(parent)
    , m_transport(transport)
    , m_playButton(new QToolButton(this))
    , m_timecode(new QLabel(this))
    , m_slider(new QSlider(Qt::Horizontal, this))
{
    m_playButton->setAutoRaise(true);
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_timecode->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_timecode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setTracking(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);
    layout->addWidget(m_playButton);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_timecode);

    connect(m_playButton, &QToolButton::clicked, this, [this] {
        if (m_transport.state() == core::Transport::State::Playing)
            m_transport.pause();
        else
            m_transport.play();
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int value) { m_transport.seek(value); });
    connect(m_slider, &QSlider::actionTriggered, this, [this](int action) {
        if (action != QAbstractSlider::SliderMove)
            m_transport.seek(m_slider->sliderPosition());
    });
    connect(&m_transport, &core::Transport::positionChanged, this, &TransportBar::updatePosition);
    connect(&m_transport, &core::Transport::durationChanged, this, &TransportBar::updateDuration);
    connect(&m_transport, &core::Transport::stateChanged, this, &TransportBar::updateState);

    updateDuration(m_transport.duration());
    updatePosition(m_transport.position());
    updateState(m_transport.state());
}

void TransportBar::updatePosition(core::FramePos position)
{
    if (!m_slider->isSliderDown()) {
        const QSignalBlocker blocker(m_slider);
        m_slider->setValue(static_cast<int>(position));
    }
    updateTimecode();
}

void TransportBar::updateDuration(core::FramePos duration)
{
    const QSignalBlocker blocker(m_slider);
    m_slider->setRange(0, static_cast<int>(std::max<core::FramePos>(0, duration - 1)));
    m_slider->setPageStep(static_cast<int>(std::max(1.0, m_transport.frameRate().toDouble())));
    m_slider->setEnabled(duration > 0);
    updateTimecode();
}

void TransportBar::updateState(core::Transport::State state)
{
    const bool playing = state == core::Transport::State::Playing;
    m_playButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    m_playButton->setToolTip(playing ? tr("Pause (Space)") : tr("Play (Space)"));
    m_playButton->setEnabled(state != core::Transport::State::Stopped);
}

void TransportBar::updateTimecode()
{
    const core::Rational rate = m_transport.frameRate();
    m_timecode->setText(
        u"%1 / %2"_s.arg(formatTimecode(m_transport.position(), rate), formatTimecode(m_transport.duration(), rate)));
}

}  // namespace mikserstudio::engine
