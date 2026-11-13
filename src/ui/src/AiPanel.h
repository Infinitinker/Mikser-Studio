// The AI panel: runs the AI operations (generate an image, generate text) and lists the session's AI jobs with
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
// media bin), "Generate text" (ai.run {op: text.generate, params: {prompt}}) and the session's jobs with
// progress, state and Cancel (ai.cancel_job). The job list polls "ai.jobs" every 500 ms while the panel is
// visible; it is a read-only command, so polls are not logged. Without ai.run and ai.jobs the panel says
// "The AI commands are not available." and disables its controls until they are registered.
class AiPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPollIntervalMs = 500;

    explicit AiPanel(core::CommandBus& bus, QWidget* parent = nullptr);

    void generateImage();
    void generateText();
    void refreshJobs();  // One poll of "ai.jobs".

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Runs ai.run with `op` and the prompt of `editor`, which is cleared once the job is queued.
    void runOperation(const QString& op, QPlainTextEdit* editor);
    void updateAvailability();
    void updateControls();
    void updatePolling();
    void showJobs(const QJsonArray& jobs);
    void cancelJob(const QString& jobId);
    void showError(const QString& message);

    core::CommandBus& m_bus;
    bool m_available = false;

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
