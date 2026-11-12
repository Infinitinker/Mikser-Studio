// A stand-in for the AI service's commands in the UI demo, so the AI panel works without the Python process.
#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QTimer>

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::ui::demo {

// Stand-in for the services module's AI commands, with the same names and result shapes
// (ai.upscale_clip, ai.generate_text, ai.jobs, ai.cancel_job), so the AI panel can be tried
// without the Python sidecar. Jobs only advance a progress value; nothing is generated: upscale
// jobs finish with a note saying so and text jobs answer with a canned sentence.
class FakeAiService final : public QObject {
    Q_OBJECT

public:
    FakeAiService(core::CommandBus& bus, const core::ProjectModel& project, QObject* parent = nullptr);
    ~FakeAiService() override;

    // Adds a job directly (for the demo's initial state).
    QString submit(const QString& op, const QString& title, double progress);

private:
    struct Job {
        QString id;
        QString op;
        QString title;
        QString state;
        double progress = 0.0;
        QString message;
        QString resultText;
    };

    void tick();
    [[nodiscard]] QJsonArray jobsJson() const;

    core::CommandBus& m_bus;
    const core::ProjectModel& m_project;
    QList<Job> m_jobs;
    QTimer m_timer;
    QStringList m_commandIds;
};

}  // namespace mikserstudio::ui::demo
