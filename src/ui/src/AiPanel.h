// The AI panel: runs the AI commands (upscale the selected clip, generate text) and lists the session's AI
// jobs with progress and Cancel.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class AiJobRow;
class ClipSelection;

// "AI" panel: "Upscale selected clip" (ai.upscale_clip {clipId, scale}), "Generate text"
// (ai.generate_text {prompt}) and the session's jobs with progress, state and Cancel
// (ai.cancel_job). The job list polls "ai.jobs" every 500 ms while the panel is visible; it is a
// read-only command, so polls are not logged. Without the AI commands the panel says "The AI service is not
// available." and disables its controls until they are registered.
class AiPanel final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kPollIntervalMs = 500;

    AiPanel(core::CommandBus& bus, const core::ProjectModel& project, const ClipSelection& selection,
            QWidget* parent = nullptr);

    void upscaleSelected();
    void generateText();
    void refreshJobs();  // One poll of "ai.jobs".

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void updateAvailability();
    void updateControls();
    void updatePolling();
    void showJobs(const QJsonArray& jobs);
    void cancelJob(const QString& jobId);
    void showError(const QString& message);

    core::CommandBus& m_bus;
    const core::ProjectModel& m_project;
    const ClipSelection& m_selection;
    bool m_available = false;

    QLabel* m_unavailable = nullptr;
    QLabel* m_clipLabel = nullptr;
    QComboBox* m_scale = nullptr;
    QPushButton* m_upscale = nullptr;
    QPlainTextEdit* m_prompt = nullptr;
    QPushButton* m_generate = nullptr;
    QLabel* m_error = nullptr;
    QLabel* m_noJobs = nullptr;
    QVBoxLayout* m_jobsLayout = nullptr;
    QHash<QString, AiJobRow*> m_rows;
    QTimer* m_poll = nullptr;
};

}  // namespace mikserstudio::ui
