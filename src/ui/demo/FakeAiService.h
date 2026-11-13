// A stand-in for the AI commands in the UI demo, so the AI panel works without Ollama or ComfyUI.
#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QTimer>

namespace mikserstudio::core {
class CommandBus;
}  // namespace mikserstudio::core

namespace mikserstudio::ui::demo {

// Stand-in for the services module's AI commands, with the same names and result shapes (ai.run,
// ai.jobs, ai.cancel_job) and the operations image.generate and text.generate, so the AI panel can
// be tried without the AI tools. Jobs only advance a progress value; nothing is generated: image
// jobs finish with a note saying so and text jobs answer with a canned sentence.
class FakeAiService final : public QObject {
    Q_OBJECT

public:
    explicit FakeAiService(core::CommandBus& bus, QObject* parent = nullptr);
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
    QList<Job> m_jobs;
    QTimer m_timer;
    QStringList m_commandIds;
};

}  // namespace mikserstudio::ui::demo
