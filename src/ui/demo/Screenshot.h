#pragma once

#include <QImage>
#include <QList>

class QWidget;

namespace mikserstudio::ui::demo {

// Grabs `main` and paints each window of `floating` over it at its place on screen (with a soft
// shadow), so a screenshot shows floating panels without capturing the rest of the desktop.
[[nodiscard]] QImage composeScreenshot(QWidget* main, const QList<QWidget*>& floating);

}  // namespace mikserstudio::ui::demo
