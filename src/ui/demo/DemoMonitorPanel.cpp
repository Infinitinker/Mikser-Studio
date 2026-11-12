#include "DemoMonitorPanel.h"

#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui::demo {

namespace {

QString timecode(core::FramePos frame, core::Rational rate)
{
    const auto fps = std::max<core::FramePos>(1, static_cast<core::FramePos>(rate.toDouble() + 0.5));
    return u"%1:%2:%3:%4"_s.arg(frame / fps / 3600, 2, 10, u'0')
        .arg(frame / fps / 60 % 60, 2, 10, u'0')
        .arg(frame / fps % 60, 2, 10, u'0')
        .arg(frame % fps, 2, 10, u'0');
}

}  // namespace

// Paints the picture area: the topmost visible video clip at the playhead, or black.
class MonitorCanvas final : public QWidget {
public:
    MonitorCanvas(QString title, core::Transport& transport, const core::ProjectModel& project, QWidget* parent)
        : QWidget(parent)
        , m_title(std::move(title))
        , m_transport(transport)
        , m_project(project)
    {
        setMinimumSize(240, 135);
        connect(&transport, &core::Transport::positionChanged, this, qOverload<>(&QWidget::update));
        connect(&project, &core::ProjectModel::timelineChanged, this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const ThemeTokens& theme = darkTheme();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), theme.window);

        const QSizeF frameSize = QSizeF(16, 9).scaled(QSizeF(size()) - QSizeF(24, 24), Qt::KeepAspectRatio);
        const QRectF frame(QPointF((width() - frameSize.width()) / 2, (height() - frameSize.height()) / 2), frameSize);
        const core::Clip* clip = clipAtPlayhead();
        const core::MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
        if (media) {
            const QColor base = media->info.kind == core::MediaKind::Image ? theme.clipImage : theme.clipVideo;
            QLinearGradient gradient(frame.topLeft(), frame.bottomRight());
            const double phase = static_cast<double>(m_transport.position() - clip->position) /
                                 std::max<double>(1.0, static_cast<double>(clip->length()));
            gradient.setColorAt(0.0, base.darker(260));
            gradient.setColorAt(std::clamp(phase, 0.05, 0.95), base.darker(140));
            gradient.setColorAt(1.0, theme.window);
            painter.fillRect(frame, gradient);
            QFont font = painter.font();
            font.setPixelSize(std::max(14, static_cast<int>(frame.height() / 12)));
            font.setWeight(QFont::DemiBold);
            painter.setFont(font);
            painter.setPen(QColor(255, 255, 255, 220));
            painter.drawText(frame, Qt::AlignCenter, media->name);
        } else {
            painter.fillRect(frame, Qt::black);
            painter.setPen(theme.textDisabled);
            painter.drawText(frame, Qt::AlignCenter, u"No clip at the playhead"_s);
        }
        QFont small = painter.font();
        small.setPixelSize(theme.fontSizeSmall);
        small.setWeight(QFont::Normal);
        painter.setFont(small);
        painter.setPen(QColor(255, 255, 255, 150));
        painter.drawText(frame.adjusted(10, 8, -10, -8), Qt::AlignTop | Qt::AlignLeft, m_title.toUpper());
        painter.setPen(theme.border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(frame);
    }

private:
    const core::Clip* clipAtPlayhead() const
    {
        const core::FramePos at = m_transport.position();
        const core::Clip* best = nullptr;
        int bestRank = -1;
        for (const core::Clip& clip : m_project.clips()) {
            const core::Track* track = m_project.findTrack(clip.trackId);
            if (!track || track->kind != core::TrackKind::Video || at < clip.position || at >= clip.end())
                continue;
            // Later video tracks (V2) are above earlier ones (V1).
            int rank = 0;
            for (qsizetype i = 0; i < m_project.tracks().size(); ++i) {
                if (m_project.tracks().at(i).id == track->id)
                    rank = static_cast<int>(i);
            }
            if (rank > bestRank) {
                best = &clip;
                bestRank = rank;
            }
        }
        return best;
    }

    QString m_title;
    core::Transport& m_transport;
    const core::ProjectModel& m_project;
};

DemoMonitorPanel::DemoMonitorPanel(const QString& title, core::Transport& transport, const core::ProjectModel& project,
                                   QWidget* parent)
    : QWidget(parent)
    , m_transport(transport)
    , m_canvas(new MonitorCanvas(title, transport, project, this))
    , m_timecode(new QLabel(this))
    , m_duration(new QLabel(this))
    , m_play(new QToolButton(this))
{
    const ThemeTokens& theme = darkTheme();
    setProperty("role", u"panel"_s);
    setAttribute(Qt::WA_StyledBackground);
    const QString mono = u"font-family: \"%1\"; font-size: %2px;"_s.arg(theme.monoFontFamily).arg(theme.fontSize);
    m_timecode->setStyleSheet(mono + u"color: %1;"_s.arg(theme.accentHover.name()));
    m_duration->setStyleSheet(mono + u"color: %1;"_s.arg(theme.textMuted.name()));
    m_play->setProperty("role", u"flat"_s);
    m_play->setFixedSize(36, 28);
    m_play->setStyleSheet(u"font-size: 15px;"_s);
    connect(m_play, &QToolButton::clicked, this, [this] {
        if (m_transport.state() == core::Transport::State::Playing)
            m_transport.pause();
        else
            m_transport.play();
    });

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(10, 0, 10, 6);
    bar->addWidget(m_timecode);
    bar->addStretch();
    bar->addWidget(m_play);
    bar->addStretch();
    bar->addWidget(m_duration);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_canvas, 1);
    layout->addLayout(bar);

    connect(&transport, &core::Transport::positionChanged, this, &DemoMonitorPanel::updateTransport);
    connect(&transport, &core::Transport::durationChanged, this, &DemoMonitorPanel::updateTransport);
    connect(&transport, &core::Transport::stateChanged, this, &DemoMonitorPanel::updateTransport);
    updateTransport();
}

void DemoMonitorPanel::updateTransport()
{
    const core::Rational rate = m_transport.frameRate();
    m_timecode->setText(timecode(m_transport.position(), rate));
    m_duration->setText(timecode(m_transport.duration(), rate));
    const bool playing = m_transport.state() == core::Transport::State::Playing;
    m_play->setText(playing ? u"❚❚"_s : u"▶"_s);
    m_play->setToolTip(playing ? u"Pause"_s : u"Play"_s);
}

}  // namespace mikserstudio::ui::demo
