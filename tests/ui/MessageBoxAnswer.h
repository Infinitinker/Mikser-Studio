#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QDeadlineTimer>
#include <QMessageBox>
#include <QString>
#include <QTimer>

namespace mikserstudio::ui::test {

// Answers the next QMessageBox that opens within a few seconds by clicking its button with
// `role` (AcceptRole: "Continue", RejectRole: "Cancel"), and records the box text in
// `*shownText`. Call it right before the code that opens the box.
inline void answerNextMessageBox(QMessageBox::ButtonRole role, QString* shownText = nullptr)
{
    auto* timer = new QTimer;
    timer->setInterval(10);
    const QDeadlineTimer deadline(5000);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, role, shownText, deadline] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) {
            if (deadline.hasExpired()) {
                timer->stop();
                timer->deleteLater();
            }
            return;
        }
        timer->stop();
        timer->deleteLater();
        if (shownText)
            *shownText = box->text();
        for (QAbstractButton* button : box->buttons()) {
            if (box->buttonRole(button) == role) {
                button->click();
                return;
            }
        }
    });
    timer->start();
}

}  // namespace mikserstudio::ui::test
