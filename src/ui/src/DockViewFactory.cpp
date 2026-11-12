// Icons for the KDDockWidgets title bar and tab bar buttons, drawn in the theme's text color.
#include "DockViewFactory.h"

#include "mikserstudio/ui/Theme.h"

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace mikserstudio::ui {

namespace {

constexpr int kIconSize = 14;

// Draws one button glyph on a 14x14 logical canvas.
void drawGlyph(QPainter& painter, KDDockWidgets::TitleBarButtonType type)
{
    using Type = KDDockWidgets::TitleBarButtonType;
    switch (type) {
    case Type::Close:
        painter.drawLine(QPointF(3.5, 3.5), QPointF(10.5, 10.5));
        painter.drawLine(QPointF(10.5, 3.5), QPointF(3.5, 10.5));
        break;
    case Type::Float:  // A window lifting out of its place.
        painter.drawRoundedRect(QRectF(5.5, 2.5, 6, 6), 1, 1);
        painter.drawPolyline(QList<QPointF>{{3.5, 5.5}, {2.5, 5.5}, {2.5, 11.5}, {8.5, 11.5}, {8.5, 10.5}});
        break;
    case Type::Minimize:
        painter.drawLine(QPointF(3.5, 10.5), QPointF(10.5, 10.5));
        break;
    case Type::Maximize:
        painter.drawRoundedRect(QRectF(3.5, 3.5, 7, 7), 1, 1);
        break;
    case Type::Normal:
        painter.drawRoundedRect(QRectF(2.5, 5.5, 6, 6), 1, 1);
        painter.drawPolyline(QList<QPointF>{{5.5, 4.5}, {5.5, 2.5}, {11.5, 2.5}, {11.5, 8.5}, {9.5, 8.5}});
        break;
    case Type::AutoHide:
    case Type::UnautoHide: {  // A pin, upright when pinned.
        painter.save();
        if (type == Type::UnautoHide) {
            painter.translate(7, 7);
            painter.rotate(45);
            painter.translate(-7, -7);
        }
        painter.drawRoundedRect(QRectF(4.5, 2.5, 5, 5), 1, 1);
        painter.drawLine(QPointF(3, 7.5), QPointF(11, 7.5));
        painter.drawLine(QPointF(7, 7.5), QPointF(7, 12));
        painter.restore();
        break;
    }
    case Type::AllTitleBarButtonTypes:
        break;
    }
}

QPixmap glyph(KDDockWidgets::TitleBarButtonType type, qreal dpr, const QColor& color)
{
    QPixmap pixmap(QSize(kIconSize, kIconSize) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    drawGlyph(painter, type);
    return pixmap;
}

}  // namespace

QIcon DockViewFactory::iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const
{
    const ThemeTokens& theme = darkTheme();
    QIcon icon;
    icon.addPixmap(glyph(type, dpr, theme.textMuted), QIcon::Normal);
    icon.addPixmap(glyph(type, dpr, theme.text), QIcon::Active);
    icon.addPixmap(glyph(type, dpr, theme.textDisabled), QIcon::Disabled);
    return icon;
}

}  // namespace mikserstudio::ui
