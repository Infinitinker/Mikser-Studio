// The AI panel: generates an image or text with the AI tools the person runs and lists the AI jobs with
// progress and Cancel.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace mikserstudio::core {
class CommandBus;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class AiJobRow;

// "AI" panel: "Generate image" (ai.run {op: image.generate, params: {prompt}}; the image comes into the
// media bin), "Generate text" (mikserstudio.ollama.generate_text {prompt}, the Ollama plugin's) and the AI
// jobs with progress, state and Cancel (jobs.cancel). The AI jobs are those of jobs.list that are part of
// no other job and are the AI engine's ("ai.*") or a plugin's own ("<owner>.*"). The list polls jobs.list
// every 500 ms while the panel is visible; it is a read-only command, so polls are not logged. A card
// whose command isn't registered is disabled; with neither, or without jobs.list, the panel says "The AI
// commands are not available." until they are registered.
class AiPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPollIntervalMs = 500;

    explicit AiPanel(core::CommandBus& bus, QWidget* parent = nullptr);

    void generateImage();
    void generateText();
    void refreshJobs();  // One poll of "jobs.list".

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Runs `command` with `params` and the prompt of `editor`, which is cleared once the job is queued.
    void runPrompt(const QString& command, const QJsonObject& params, QPlainTextEdit* editor);
    void updateAvailability();
    void updateControls();
    void updatePolling();
    void showJobs(const QJsonArray& jobs);
    void cancelJob(const QString& jobId);
    void showError(const QString& message);

    core::CommandBus& m_bus;
    bool m_available = false;
    bool m_imageAvailable = false;
    bool m_textAvailable = false;

    QLabel* m_unavailable = nullptr;
    QPlainTextEdit* m_imagePrompt = nullptr;
    QPushButton* m_generateImage = nullptr;
    QPlainTextEdit* m_prompt = nullptr;
    QPushButton* m_generate = nullptr;
    QLabel* m_error = nullptr;
    QLabel* m_noJobs = nullptr;
    QVBoxLayout* m_jobsLayout = nullptr;
    QHash<QString, AiJobRow*> m_rows;
    QTimer* m_poll = nullptr;
};

}  // namespace mikserstudio::ui
