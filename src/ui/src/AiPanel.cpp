// The AI panel: the prompt cards that run AI operations, the job list polled from "jobs.list" and the
// unavailable state.
#include "AiPanel.h"

#include "AiJobRow.h"
#include "CommandInvoker.h"
#include "PanelLayout.h"

#include "mikserstudio/core/CommandBus.h"

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

const auto kRun = u"ai.run"_s;
const auto kText = u"mikserstudio.ollama.generate_text"_s;
const auto kJobs = u"jobs.list"_s;
const auto kCancel = u"jobs.cancel"_s;
const auto kImageOp = u"image.generate"_s;

// A job of the AI engine, or a plugin's own, that is part of no other: one of the person's AI jobs. The
// jobs a plugin's job starts, such as its HTTP requests, show on it.
bool isAiJob(const QJsonObject& job)
{
    if (!job.value(u"parent").toString().isEmpty())
        return false;
    const QString kind = job.value(u"kind").toString();
    const QString owner = job.value(u"owner").toString();
    return kind.startsWith(u"ai."_s) || (owner != u"core" && kind.startsWith(owner + u"."_s));
}

// A card with a heading, a prompt editor and a primary Generate button.
struct PromptCard {
    QFrame* frame = nullptr;
    QPlainTextEdit* prompt = nullptr;
    QPushButton* button = nullptr;
};

PromptCard makePromptCard(const QString& heading, const QString& placeholder, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setProperty("role", u"card"_s);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(heading, u"heading"_s, frame));
    auto* prompt = new QPlainTextEdit(frame);
    prompt->setPlaceholderText(placeholder);
    prompt->setFixedHeight(72);
    prompt->setTabChangesFocus(true);
    layout->addWidget(prompt);
    auto* row = new QHBoxLayout;
    auto* button = new QPushButton(u"Generate"_s, frame);
    button->setProperty("role", u"primary"_s);
    row->addStretch();
    row->addWidget(button);
    layout->addLayout(row);
    return PromptCard{.frame = frame, .prompt = prompt, .button = button};
}

}  // namespace

AiPanel::AiPanel(core::CommandBus& bus, QWidget* parent)
    : QWidget(parent)
    , m_bus(bus)
    , m_poll(new QTimer(this))
{
    auto* layout = makePanelLayout(this);
    layout->addLayout(makePanelHeader(u"AI"_s));

    m_unavailable = makeLabel(u"The AI commands are not available."_s, u"placeholder"_s, this);
    m_unavailable->setObjectName(u"unavailable"_s);
    m_unavailable->setWordWrap(true);
    layout->addWidget(m_unavailable);

    const PromptCard imageCard = makePromptCard(u"Generate image"_s, u"Describe the image…"_s, this);
    m_imagePrompt = imageCard.prompt;
    m_imagePrompt->setObjectName(u"imagePrompt"_s);
    m_generateImage = imageCard.button;
    m_generateImage->setObjectName(u"generateImage"_s);
    m_generateImage->setToolTip(u"Makes the image in ComfyUI and adds it to the media bin."_s);
    layout->addWidget(imageCard.frame);

    const PromptCard textCard = makePromptCard(u"Generate text"_s, u"Ask a language model in Ollama…"_s, this);
    m_prompt = textCard.prompt;
    m_prompt->setObjectName(u"prompt"_s);
    m_generate = textCard.button;
    m_generate->setObjectName(u"generate"_s);
    m_generate->setToolTip(u"Asks a language model in Ollama; the answer shows in its job below."_s);
    layout->addWidget(textCard.frame);

    m_error = makeLabel({}, u"error"_s, this);
    m_error->setObjectName(u"error"_s);
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

    connect(m_generateImage, &QPushButton::clicked, this, &AiPanel::generateImage);
    connect(m_generate, &QPushButton::clicked, this, &AiPanel::generateText);
    connect(m_imagePrompt, &QPlainTextEdit::textChanged, this, &AiPanel::updateControls);
    connect(m_prompt, &QPlainTextEdit::textChanged, this, &AiPanel::updateControls);
    connect(&bus, &core::CommandBus::commandRegistered, this, &AiPanel::updateAvailability);
    connect(&bus, &core::CommandBus::commandUnregistered, this, &AiPanel::updateAvailability);
    connect(&bus, &core::CommandBus::commandChanged, this, &AiPanel::updateAvailability);
    m_poll->setObjectName(u"jobPoll"_s);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &AiPanel::refreshJobs);
    updateAvailability();
}

void AiPanel::generateImage()
{
    if (!m_imageAvailable)
        return;
    const QString prompt = m_imagePrompt->toPlainText().trimmed();
    runPrompt(kRun, QJsonObject{{u"op"_s, kImageOp}, {u"params"_s, QJsonObject{{u"prompt"_s, prompt}}}}, m_imagePrompt);
}

void AiPanel::generateText()
{
    if (!m_textAvailable)
        return;
    runPrompt(kText, QJsonObject{{u"prompt"_s, m_prompt->toPlainText().trimmed()}}, m_prompt);
}

void AiPanel::runPrompt(const QString& command, const QJsonObject& params, QPlainTextEdit* editor)
{
    if (!m_available || editor->toPlainText().trimmed().isEmpty())
        return;
    const auto run = executeInteractive(m_bus, command, params, this);
    if (!run)
        return;
    if (const auto& result = *run; !result) {
        showError(result.error().message);
        return;
    }
    m_error->hide();
    editor->clear();
    refreshJobs();
}

void AiPanel::refreshJobs()
{
    if (!m_bus.contains(kJobs))
        return;
    // jobs.list is read-only, so these polls stay out of the log and the status bar.
    const auto result = m_bus.execute(kJobs);
    if (!result)
        return;
    QJsonArray jobs;
    for (const QJsonValue job : result->value(u"jobs").toArray()) {
        if (isAiJob(job.toObject()))
            jobs.append(job);
    }
    showJobs(jobs);
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
    const bool jobs = m_bus.contains(kJobs) && m_bus.contains(kCancel);
    m_imageAvailable = jobs && m_bus.contains(kRun);
    m_textAvailable = jobs && m_bus.contains(kText);
    m_available = m_imageAvailable || m_textAvailable;
    m_unavailable->setVisible(!m_available);
    updateControls();
    updatePolling();
}

void AiPanel::updateControls()
{
    m_imagePrompt->setEnabled(m_imageAvailable);
    m_generateImage->setEnabled(m_imageAvailable && !m_imagePrompt->toPlainText().trimmed().isEmpty());
    m_prompt->setEnabled(m_textAvailable);
    m_generate->setEnabled(m_textAvailable && !m_prompt->toPlainText().trimmed().isEmpty());
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
