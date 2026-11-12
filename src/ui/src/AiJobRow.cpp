#include "AiJobRow.h"

#include "mikserstudio/ui/Theme.h"

#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr qsizetype kResultChars = 600;

QColor stateColor(const QString& state)
{
    const ThemeTokens& theme = darkTheme();
    if (state == u"succeeded")
        return theme.success;
    if (state == u"failed")
        return theme.danger;
    if (state == u"running")
        return theme.accentHover;
    return theme.textMuted;
}

bool isFinished(const QString& state)
{
    return state == u"succeeded" || state == u"failed" || state == u"canceled";
}

}  // namespace

AiJobRow::AiJobRow(QWidget* parent)
    : QFrame(parent)
    , m_title(new QLabel(this))
    , m_stateLabel(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_detail(new QLabel(this))
    , m_result(new QLabel(this))
    , m_cancel(new QPushButton(u"Cancel"_s, this))
{
    setProperty("role", u"card"_s);
    m_title->setWordWrap(true);
    m_stateLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_detail->setProperty("role", u"hint"_s);
    m_detail->setWordWrap(true);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_result->hide();
    m_cancel->setToolTip(u"Stops the job (ai.cancel_job)."_s);
    connect(m_cancel, &QPushButton::clicked, this, [this] { Q_EMIT cancelRequested(m_jobId); });

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(6);
    layout->addWidget(m_title, 0, 0);
    layout->addWidget(m_stateLabel, 0, 1);
    layout->addWidget(m_progress, 1, 0, 1, 2);
    layout->addWidget(m_detail, 2, 0);
    layout->addWidget(m_cancel, 2, 1, Qt::AlignRight);
    layout->addWidget(m_result, 3, 0, 1, 2);
    layout->setColumnStretch(0, 1);
}

void AiJobRow::update(const QJsonObject& job)
{
    m_jobId = job.value(u"id").toString();
    m_state = job.value(u"state").toString();
    const double progress = std::clamp(job.value(u"progress").toDouble(), 0.0, 1.0);

    m_title->setText(job.value(u"title").toString(m_jobId));
    m_stateLabel->setText(m_state.isEmpty() ? QString() : m_state.at(0).toUpper() + m_state.mid(1));
    m_stateLabel->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(stateColor(m_state).name()));
    m_progress->setValue(static_cast<int>(std::lround(progress * 1000.0)));
    m_progress->setVisible(!isFinished(m_state) || m_state == u"succeeded");

    const QString error = job.value(u"error").toString();
    const QString message = job.value(u"message").toString();
    m_detail->setText(!error.isEmpty() ? error : message.isEmpty() ? u"%1 %"_s.arg(std::lround(progress * 100.0)) : message);
    m_detail->setStyleSheet(error.isEmpty() ? QString() : u"color: %1;"_s.arg(darkTheme().danger.name()));
    m_cancel->setVisible(!isFinished(m_state));

    const QString text = job.value(u"result").toObject().value(u"text").toString();
    m_result->setVisible(!text.isEmpty());
    m_result->setText(text.size() > kResultChars ? text.left(kResultChars - 1) + u"…"_s : text);
    m_result->setToolTip(text);
}

}  // namespace mikserstudio::ui
