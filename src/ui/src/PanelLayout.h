#pragma once

#include <QHBoxLayout>
#include <QList>
#include <QString>
#include <QVBoxLayout>

class QLabel;
class QWidget;

namespace mikserstudio::ui {

// Gives `panel` the panel background and a vertical layout with the standard margins.
QVBoxLayout* makePanelLayout(QWidget* panel);
// A header row: a heading on the left and `trailing` widgets (buttons) on the right.
QHBoxLayout* makePanelHeader(const QString& heading, const QList<QWidget*>& trailing = {});
// A label with a theme role ("muted", "hint", "error", "heading", "placeholder").
QLabel* makeLabel(const QString& text, const QString& role, QWidget* parent = nullptr);

}  // namespace mikserstudio::ui
