#include "Screenshot.h"

#include <QPainter>
#include <QPixmap>
#include <QWidget>

namespace mikserstudio::ui::demo {

QImage composeScreenshot(QWidget* main, const QList<QWidget*>& floating)
{
    QImage image = main->grab().toImage();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPoint origin = main->mapToGlobal(QPoint(0, 0));
    for (QWidget* window : floating) {
        if (!window || !window->isVisible() || window == main)
            continue;
        const QPixmap pixmap = window->grab();
        const QPoint offset = window->mapToGlobal(QPoint(0, 0)) - origin;
        const QRect area(offset, window->size());
        constexpr int kShadow = 14;
        for (int i = kShadow; i > 0; i -= 2) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 10));
            painter.drawRoundedRect(area.adjusted(-i, -i + 4, i, i + 4), i, i);
        }
        painter.drawPixmap(offset, pixmap);
    }
    painter.end();
    return image;
}

}  // namespace mikserstudio::ui::demo
