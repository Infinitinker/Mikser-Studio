// One AI job in the AI panel: title, state, progress, message or error, result text and a Cancel button.
#pragma once

#include <QFrame>
#include <QJsonObject>

class QLabel;
class QProgressBar;
class QPushButton;

namespace mikserstudio::ui {

// One AI job in the AI panel: title, state, progress, message or error, result text and Cancel.
// `job` is an entry of the "jobs.list" result.
class AiJobRow final : public QFrame {
    Q_OBJECT

public:
    explicit AiJobRow(QWidget* parent = nullptr);

    void update(const QJsonObject& job);
    [[nodiscard]] QString jobId() const { return m_jobId; }
    [[nodiscard]] QString state() const { return m_state; }

Q_SIGNALS:
    void cancelRequested(const QString& jobId);

private:
    QString m_jobId;
    QString m_state;
    QLabel* m_title;
    QLabel* m_stateLabel;
    QProgressBar* m_progress;
    QLabel* m_detail;
    QLabel* m_result;
    QPushButton* m_cancel;
};

}  // namespace mikserstudio::ui
