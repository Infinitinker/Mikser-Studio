#pragma once

#include "mikserstudio/engine/FrameSink.h"

#include <rhi/qrhi.h>

#include <QImage>
#include <QSize>

#include <expected>
#include <memory>
#include <optional>

class QWindow;

namespace mikserstudio::engine {

// The QRhi side of a MonitorWindow: device, swapchain, the frame texture and the letterboxed
// textured quad. The device and texture live as long as the renderer; the swapchain (and the
// pipeline tied to its render pass) is released when the native surface goes away and recreated
// on the next render.
class MonitorRenderer {
public:
    MonitorRenderer(QWindow& window, QRhi::Implementation backend);
    ~MonitorRenderer();
    MonitorRenderer(const MonitorRenderer&) = delete;
    MonitorRenderer& operator=(const MonitorRenderer&) = delete;

    enum class Result { Rendered, Skipped, RetryLater };

    // Draws one frame: `frame` (if any) replaces the picture first; a frame that arrives while
    // the window cannot draw is kept and shown on the next render. With `readback`, the rendered
    // swapchain image is read back into it.
    Result render(std::optional<VideoFrame> frame, std::expected<QImage, QString>* readback = nullptr);

    void releaseSwapChain();  // Before the native surface is destroyed.

    [[nodiscard]] quint64 framesPresented() const { return m_presented; }
    [[nodiscard]] QString backendName() const;

private:
    bool ensureDevice();
    bool ensureSwapChain();
    bool ensurePipeline();
    void ensureTexture(QSize size);
    void releaseDevice();
    void updateTransform(QRhiResourceUpdateBatch& updates);
    [[nodiscard]] std::expected<QImage, QString> toImage(const QRhiReadbackResult& result) const;

    QWindow& m_window;
    QRhi::Implementation m_backend;
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiSwapChain> m_swapChain;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiBuffer> m_vertices;
    std::unique_ptr<QRhiBuffer> m_transform;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiShaderResourceBindings> m_bindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    bool m_verticesUploaded = false;
    QSize m_frameSize;
    QImage m_lastImage;  // Newest frame; re-uploaded when the texture is recreated.
    bool m_textureStale = false;  // m_lastImage is newer than the texture.
    quint64 m_presented = 0;
};

}  // namespace mikserstudio::engine
