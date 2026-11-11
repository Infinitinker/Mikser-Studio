#include "TimelineView.h"

#include "Logging.h"
#include "MediaBinModel.h"
#include "TimelineController.h"

#include "mikserstudio/ui/Theme.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>

#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString droppedMediaId(const QMimeData* mime)
{
    return QString::fromUtf8(mime->data(QString::fromLatin1(MediaBinModel::kMediaIdMimeType)));
}

}  // namespace

TimelineView::TimelineView(TimelineController& controller, ThemeObject& theme, QWidget* parent)
    : QQuickWidget(parent)
    , m_controller(controller)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(theme.panel());
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMinimumHeight(controller.rulerHeight() + controller.trackHeight() * 2);
    rootContext()->setContextProperty(u"timeline"_s, &controller);
    rootContext()->setContextProperty(u"theme"_s, &theme);
    connect(this, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status status) {
        if (status != QQuickWidget::Error)
            return;
        for (const QQmlError& error : errors())
            qCWarning(lcPanels).noquote() << "Timeline QML:" << error.toString();
    });
    setSource(QUrl(u"qrc:/mikserstudio/ui/qml/Timeline.qml"_s));
}

void TimelineView::dragEnterEvent(QDragEnterEvent* event)
{
    const QString mediaId = droppedMediaId(event->mimeData());
    if (mediaId.isEmpty()) {
        event->ignore();
        return;
    }
    const QPointF at = toTrackArea(event->position());
    m_controller.updateDrop(mediaId, at.x(), at.y());
    event->acceptProposedAction();
}

void TimelineView::dragMoveEvent(QDragMoveEvent* event)
{
    const QString mediaId = droppedMediaId(event->mimeData());
    if (mediaId.isEmpty()) {
        event->ignore();
        return;
    }
    const QPointF at = toTrackArea(event->position());
    if (m_controller.updateDrop(mediaId, at.x(), at.y()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_controller.clearDrop();
    event->accept();
}

void TimelineView::dropEvent(QDropEvent* event)
{
    const QString mediaId = droppedMediaId(event->mimeData());
    if (mediaId.isEmpty()) {
        event->ignore();
        return;
    }
    const QPointF at = toTrackArea(event->position());
    m_controller.updateDrop(mediaId, at.x(), at.y());
    if (m_controller.commitDrop(mediaId))
        event->acceptProposedAction();
    else
        event->ignore();
    setFocus();
}

void TimelineView::wheelEvent(QWheelEvent* event)
{
    const QPoint delta = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        const double steps = delta.y() / 120.0;
        m_controller.zoomAt(std::pow(1.2, steps), toTrackArea(event->position()).x());
    } else if (event->modifiers() & Qt::ShiftModifier) {
        // Some platforms turn Shift+wheel into a horizontal delta.
        const int amount = delta.y() != 0 ? delta.y() : delta.x();
        m_controller.scrollTracksBy(-amount * 0.5);
    } else {
        // Vertical wheels scroll time too; horizontal wheels and touchpads scroll directly.
        const int amount = delta.x() != 0 ? delta.x() : delta.y();
        m_controller.scrollBy(-amount * 0.8);
    }
    event->accept();
}

void TimelineView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_controller.removeSelected();
        event->accept();
        return;
    case Qt::Key_Backslash:
        m_controller.zoomToFit();
        event->accept();
        return;
    case Qt::Key_S:
        if (event->modifiers() == Qt::NoModifier) {
            m_controller.toggleSnapping();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Home:
        m_controller.seek(0);
        event->accept();
        return;
    case Qt::Key_End:
        m_controller.seek(std::max<qint64>(0, m_controller.duration() - 1));
        event->accept();
        return;
    default:
        break;
    }
    QQuickWidget::keyPressEvent(event);
}

QPointF TimelineView::toTrackArea(QPointF position) const
{
    return {position.x() - m_controller.headerWidth(), position.y() - m_controller.rulerHeight()};
}

}  // namespace mikserstudio::ui
