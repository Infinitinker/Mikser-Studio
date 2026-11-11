#include "AiPanel.h"

#include "AiJobRow.h"
#include "CommandInvoker.h"
#include "PanelLayout.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/ui/ClipSelection.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

const auto kUpscale = u"ai.upscale_clip"_s;
const auto kGenerate = u"ai.generate_text"_s;
const auto kJobs = u"ai.jobs"_s;
const auto kCancel = u"ai.cancel_job"_s;

QFrame* card(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("role", u"card"_s);
    return frame;
}

}  // namespace

AiPanel::AiPanel(core::CommandBus& bus, const core::ProjectModel& project, const ClipSelection& selection,
                 QWidget* parent)
    : QWidget(parent)
    , m_bus(bus)
    , m_project(project)
    , m_selection(selection)
    , m_poll(new QTimer(this))
{
    auto* layout = makePanelLayout(this);
    layout->addLayout(makePanelHeader(u"AI"_s));

    m_unavailable = makeLabel(u"The AI service is not available."_s, u"placeholder"_s, this);
    m_unavailable->setWordWrap(true);
    layout->addWidget(m_unavailable);

    // Upscale.
    auto* upscaleCard = card(this);
    auto* upscaleLayout = new QVBoxLayout(upscaleCard);
    upscaleLayout->setContentsMargins(12, 10, 12, 12);
    upscaleLayout->setSpacing(8);
    upscaleLayout->addWidget(makeLabel(u"Upscale selected clip"_s, u"heading"_s, upscaleCard));
    m_clipLabel = makeLabel({}, u"muted"_s, upscaleCard);
    m_clipLabel->setWordWrap(true);
    upscaleLayout->addWidget(m_clipLabel);
    auto* upscaleRow = new QHBoxLayout;
    upscaleRow->setSpacing(8);
    m_scale = new QComboBox(upscaleCard);
    for (const int factor : {2, 3, 4})
        m_scale->addItem(u"%1×"_s.arg(factor), factor);
    m_scale->setToolTip(u"Scale factor of the upscaled clip."_s);
    m_upscale = new QPushButton(u"Upscale"_s, upscaleCard);
    m_upscale->setProperty("role", u"primary"_s);
    upscaleRow->addWidget(m_scale);
    upscaleRow->addStretch();
    upscaleRow->addWidget(m_upscale);
    upscaleLayout->addLayout(upscaleRow);
    layout->addWidget(upscaleCard);

    // Generate text.
    auto* textCard = card(this);
    auto* textLayout = new QVBoxLayout(textCard);
    textLayout->setContentsMargins(12, 10, 12, 12);
    textLayout->setSpacing(8);
    textLayout->addWidget(makeLabel(u"Generate text"_s, u"heading"_s, textCard));
    m_prompt = new QPlainTextEdit(textCard);
    m_prompt->setPlaceholderText(u"Ask the local language model…"_s);
    m_prompt->setFixedHeight(72);
    m_prompt->setTabChangesFocus(true);
    textLayout->addWidget(m_prompt);
    auto* textRow = new QHBoxLayout;
    m_generate = new QPushButton(u"Generate"_s, textCard);
    m_generate->setProperty("role", u"primary"_s);
    textRow->addStretch();
    textRow->addWidget(m_generate);
    textLayout->addLayout(textRow);
    layout->addWidget(textCard);

    m_error = makeLabel({}, u"error"_s, this);
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    // Jobs.
    layout->addWidget(makeLabel(u"Jobs"_s, u"heading"_s, this));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* jobsHost = new QWidget(scroll);
    jobsHost->setProperty("role", u"panel"_s);
    m_jobsLayout = new QVBoxLayout(jobsHost);
    m_jobsLayout->setContentsMargins(0, 0, 0, 0);
    m_jobsLayout->setSpacing(6);
    m_noJobs = makeLabel(u"No AI jobs in this session yet."_s, u"muted"_s, jobsHost);
    m_jobsLayout->addWidget(m_noJobs);
    m_jobsLayout->addStretch();
    scroll->setWidget(jobsHost);
    layout->addWidget(scroll, 1);

    connect(m_upscale, &QPushButton::clicked, this, &AiPanel::upscaleSelected);
    connect(m_generate, &QPushButton::clicked, this, &AiPanel::generateText);
    connect(m_prompt, &QPlainTextEdit::textChanged, this, &AiPanel::updateControls);
    connect(&selection, &ClipSelection::changed, this, &AiPanel::updateControls);
    connect(&project, &core::ProjectModel::clipChanged, this, &AiPanel::updateControls);
    connect(&bus, &core::CommandBus::commandRegistered, this, &AiPanel::updateAvailability);
    connect(&bus, &core::CommandBus::commandUnregistered, this, &AiPanel::updateAvailability);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &AiPanel::refreshJobs);
    updateAvailability();
}

bool AiPanel::isPolling() const
{
    return m_poll->isActive();
}

QString AiPanel::errorText() const
{
    return m_error->isHidden() ? QString() : m_error->text();
}

void AiPanel::upscaleSelected()
{
    const QString clipId = m_selection.current();
    if (!m_available || clipId.isEmpty())
        return;
    const auto run = executeInteractive(
        m_bus, kUpscale, QJsonObject{{u"clipId"_s, clipId}, {u"scale"_s, m_scale->currentData().toInt()}}, this);
    if (!run)
        return;
    if (const auto& result = *run; !result) {
        showError(result.error().message);
        return;
    }
    m_error->hide();
    refreshJobs();
}

void AiPanel::generateText()
{
    const QString prompt = m_prompt->toPlainText().trimmed();
    if (!m_available || prompt.isEmpty())
        return;
    const auto run = executeInteractive(m_bus, kGenerate, QJsonObject{{u"prompt"_s, prompt}}, this);
    if (!run)
        return;
    if (const auto& result = *run; !result) {
        showError(result.error().message);
        return;
    }
    m_error->hide();
    m_prompt->clear();
    refreshJobs();
}

void AiPanel::refreshJobs()
{
    if (!m_bus.contains(kJobs))
        return;
    // ai.jobs is read-only, so these polls stay out of the log and the status bar.
    const auto result = m_bus.execute(kJobs);
    if (result)
        showJobs(result->value(u"jobs").toArray());
}

void AiPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updatePolling();
    refreshJobs();
}

void AiPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    updatePolling();
}

void AiPanel::updateAvailability()
{
    const bool available = m_bus.contains(kUpscale) && m_bus.contains(kGenerate) && m_bus.contains(kJobs);
    m_available = available;
    m_unavailable->setVisible(!available);
    updateControls();
    updatePolling();
}

void AiPanel::updateControls()
{
    const core::Clip* clip = m_project.findClip(m_selection.current());
    const core::MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    const bool isVideo = media && media->info.kind == core::MediaKind::Video;
    if (!clip)
        m_clipLabel->setText(u"Select a video clip on the timeline."_s);
    else if (!isVideo)
        m_clipLabel->setText(u"\"%1\" is not a video clip."_s.arg(media ? media->name : clip->id));
    else
        m_clipLabel->setText(u"Clip: %1"_s.arg(media->name));

    m_scale->setEnabled(m_available);
    m_upscale->setEnabled(m_available && isVideo);
    m_prompt->setEnabled(m_available);
    m_generate->setEnabled(m_available && !m_prompt->toPlainText().trimmed().isEmpty());
}

void AiPanel::updatePolling()
{
    const bool poll = m_available && isVisible();
    if (poll && !m_poll->isActive())
        m_poll->start();
    else if (!poll)
        m_poll->stop();
}

void AiPanel::showJobs(const QJsonArray& jobs)
{
    QSet<QString> seen;
    // Newest first: the jobs come in submission order.
    for (qsizetype i = jobs.size() - 1; i >= 0; --i) {
        const QJsonObject job = jobs.at(i).toObject();
        const QString id = job.value(u"id").toString();
        if (id.isEmpty())
            continue;
        seen.insert(id);
        AiJobRow* row = m_rows.value(id);
        if (!row) {
            row = new AiJobRow(m_jobsLayout->parentWidget());
            connect(row, &AiJobRow::cancelRequested, this, &AiPanel::cancelJob);
            m_rows.insert(id, row);
        }
        row->update(job);
        m_jobsLayout->removeWidget(row);
        m_jobsLayout->insertWidget(static_cast<int>(jobs.size() - 1 - i), row);
    }
    for (auto it = m_rows.begin(); it != m_rows.end();) {
        if (seen.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->deleteLater();
        it = m_rows.erase(it);
    }
    m_noJobs->setVisible(m_rows.isEmpty());
}

void AiPanel::cancelJob(const QString& jobId)
{
    if (!m_bus.contains(kCancel))
        return;
    const auto run = executeInteractive(m_bus, kCancel, QJsonObject{{u"jobId"_s, jobId}}, this);
    if (!run)
        return;
    if (const auto& result = *run; !result)
        showError(result.error().message);
    refreshJobs();
}

void AiPanel::showError(const QString& message)
{
    m_error->setText(message);
    m_error->show();
}

}  // namespace mikserstudio::ui
