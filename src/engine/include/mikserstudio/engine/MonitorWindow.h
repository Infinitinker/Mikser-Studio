#pragma once

#include "mikserstudio/engine/FrameSink.h"

#include <QImage>
#include <QWindow>

#include <expected>
#include <memory>

namespace mikserstudio::engine {

class FrameMailbox;
class MonitorRenderer;

// The video surface of a monitor: a QWindow drawn with QRhi (D3D11 on Windows, Metal on macOS,
// Vulkan or OpenGL on Linux; the Null backend on the offscreen/minimal platforms or when
// $MIKSERSTUDIO_MONITOR_RHI=null). Embed it with QWidget::createWindowContainer.
//
// Only the newest frame is kept: frames arriving faster than the window draws are dropped and
// counted. Drawing happens on demand (requestUpdate when a frame arrives, expose, resize); the
// swapchain is released when the native surface goes away (re-parenting a floated dock) and
// recreated on the next expose. The frame is letterboxed, aspect-correct, on black.
class MonitorWindow final : public QWindow {
    Q_OBJECT

public:
    explicit MonitorWindow(QWindow* parent = nullptr);
    ~MonitorWindow() override;

    // Give this to a player. Stays valid (and harmless) after the window is gone.
    [[nodiscard]] std::shared_ptr<FrameSink> frameSink() const;

    [[nodiscard]] quint64 framesReceived() const;
    [[nodiscard]] quint64 framesPresented() const;  // Uploaded and drawn.
    [[nodiscard]] quint64 framesDropped() const;    // Replaced by a newer one before being drawn.
    [[nodiscard]] QString backendName() const;      // "D3D11", "Null", ... once initialized.

    // Draws the newest frame and reads the result back from the swapchain. Needs an exposed
    // window with a non-empty size.
    std::expected<QImage, QString> grabFramebuffer();

Q_SIGNALS:
    // Key presses on the video surface, forwarded because they do not reach parent widgets.
    void keyPressed(int key, Qt::KeyboardModifiers modifiers);

protected:
    bool event(QEvent* event) override;
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void renderNow();

    std::shared_ptr<FrameMailbox> m_mailbox;
    std::unique_ptr<MonitorRenderer> m_renderer;
};

}  // namespace mikserstudio::engine
