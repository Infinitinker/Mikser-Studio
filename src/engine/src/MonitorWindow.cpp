#include "mikserstudio/engine/MonitorWindow.h"

#include "FrameMailbox.h"
#include "Logging.h"
#include "MonitorRenderer.h"
#include "RhiBackend.h"

#include <QExposeEvent>
#include <QKeyEvent>
#include <QPlatformSurfaceEvent>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

MonitorWindow::MonitorWindow(QWindow* parent)
    : QWindow(parent)
    , m_mailbox(std::make_shared<FrameMailbox>())
{
    const QRhi::Implementation backend = preferredRhiBackend();
    prepareWindow(*this, backend);
    m_renderer = std::make_unique<MonitorRenderer>(*this, backend);

    // Runs on the MLT thread with the mailbox locked; the destructor clears it under the same lock,
    // so the window is alive here. Qt drops the queued call if the window is deleted before it runs.
    m_mailbox->setListener([this] { QMetaObject::invokeMethod(this, &QWindow::requestUpdate, Qt::QueuedConnection); });
}

MonitorWindow::~MonitorWindow()
{
    m_mailbox->setListener({});
    m_renderer.reset();
}

std::shared_ptr<FrameSink> MonitorWindow::frameSink() const
{
    return m_mailbox;
}

quint64 MonitorWindow::framesReceived() const
{
    return m_mailbox->received();
}

quint64 MonitorWindow::framesPresented() const
{
    return m_renderer->framesPresented();
}

quint64 MonitorWindow::framesDropped() const
{
    return m_mailbox->dropped();
}

QString MonitorWindow::backendName() const
{
    return m_renderer->backendName();
}

std::expected<QImage, QString> MonitorWindow::grabFramebuffer()
{
    std::expected<QImage, QString> image = std::unexpected(u"The monitor is not visible, so nothing was drawn."_s);
    m_renderer->render(m_mailbox->take(), &image);
    return image;
}

void MonitorWindow::renderNow()
{
    if (m_renderer->render(m_mailbox->take()) == MonitorRenderer::Result::RetryLater)
        requestUpdate();
}

bool MonitorWindow::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::UpdateRequest:
        renderNow();
        return true;
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            qCDebug(lcMonitor) << "Native surface going away; releasing the swapchain";
            m_renderer->releaseSwapChain();
        }
        break;
    default:
        break;
    }
    return QWindow::event(event);
}

void MonitorWindow::exposeEvent(QExposeEvent*)
{
    if (isExposed())
        renderNow();
}

void MonitorWindow::resizeEvent(QResizeEvent*)
{
    if (isExposed())
        requestUpdate();
}

void MonitorWindow::keyPressEvent(QKeyEvent* event)
{
    Q_EMIT keyPressed(event->key(), event->modifiers());
    event->accept();
}

}  // namespace mikserstudio::engine
