// The QRhi drawing of a monitor window: uploading the newest frame and drawing it letterboxed.
#include "MonitorRenderer.h"

#include "Logging.h"
#include "RhiBackend.h"

#include <rhi/qshader.h>

#include <QFile>
#include <QMatrix4x4>
#include <QWindow>

#include <array>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

// Triangle strip covering clip space: x, y, u, v. Texture row 0 is the top of the image.
constexpr std::array<float, 16> kQuad = {
    -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
};
constexpr quint32 kVertexStride = 4 * sizeof(float);
constexpr quint32 kTransformSize = 16 * sizeof(float);

QShader loadShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(file.readAll());
}

}  // namespace

MonitorRenderer::MonitorRenderer(QWindow& window, QRhi::Implementation backend)
    : m_window(window)
    , m_backend(backend)
{
}

MonitorRenderer::~MonitorRenderer()
{
    releaseDevice();
}

QString MonitorRenderer::backendName() const
{
    return QString::fromLatin1(m_rhi ? m_rhi->backendName() : QRhi::backendName(m_backend));
}

bool MonitorRenderer::ensureDevice()
{
    if (m_rhi)
        return true;
    m_rhi = createRhi(m_backend, m_window);
    if (!m_rhi)
        return false;

    m_vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad)));
    m_transform.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kTransformSize));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!m_vertices->create() || !m_transform->create() || !m_sampler->create()) {
        qCWarning(lcMonitor) << "Could not create the monitor's GPU buffers";
        releaseDevice();
        return false;
    }
    m_verticesUploaded = false;
    qCInfo(lcMonitor).noquote() << "Monitor renders with" << backendName() << "on"
                                << QString::fromUtf8(m_rhi->driverInfo().deviceName);
    return true;
}

bool MonitorRenderer::ensureSwapChain()
{
    if (m_swapChain)
        return true;
    m_swapChain.reset(m_rhi->newSwapChain());
    m_swapChain->setWindow(&m_window);
    m_renderPass.reset(m_swapChain->newCompatibleRenderPassDescriptor());
    m_swapChain->setRenderPassDescriptor(m_renderPass.get());
    if (!m_swapChain->createOrResize()) {
        qCWarning(lcMonitor) << "Could not create the monitor swapchain";
        releaseSwapChain();
        return false;
    }
    return true;
}

void MonitorRenderer::ensureTexture(QSize size)
{
    if (m_texture && m_frameSize == size)
        return;
    m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size));
    if (!m_texture->create()) {
        qCWarning(lcMonitor) << "Could not create a" << size << "monitor texture";
        m_texture.reset();
        m_frameSize = {};
        return;
    }
    m_frameSize = size;
    if (!m_bindings)
        m_bindings.reset(m_rhi->newShaderResourceBindings());
    m_bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_transform.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_texture.get(),
                                                  m_sampler.get()),
    });
    // Same layout as before, so an existing pipeline stays compatible.
    m_bindings->create();
}

bool MonitorRenderer::ensurePipeline()
{
    if (m_pipeline)
        return true;
    if (!m_bindings || !m_renderPass)
        return false;

    const QShader vertex = loadShader(u":/mikserstudio/engine/monitor.vert.qsb"_s);
    const QShader fragment = loadShader(u":/mikserstudio/engine/monitor.frag.qsb"_s);
    if (!vertex.isValid() || !fragment.isValid()) {
        qCCritical(lcMonitor) << "The monitor shaders are missing from the application resources";
        return false;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{kVertexStride}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_bindings.get());
    m_pipeline->setRenderPassDescriptor(m_renderPass.get());
    if (!m_pipeline->create()) {
        qCWarning(lcMonitor) << "Could not create the monitor pipeline";
        m_pipeline.reset();
        return false;
    }
    return true;
}

void MonitorRenderer::updateTransform(QRhiResourceUpdateBatch& updates)
{
    const QSize output = m_swapChain->currentPixelSize();
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (!m_frameSize.isEmpty() && !output.isEmpty()) {
        const double frameAspect = static_cast<double>(m_frameSize.width()) / m_frameSize.height();
        const double outputAspect = static_cast<double>(output.width()) / output.height();
        if (frameAspect > outputAspect)
            scaleY = static_cast<float>(outputAspect / frameAspect);
        else
            scaleX = static_cast<float>(frameAspect / outputAspect);
    }
    QMatrix4x4 transform = m_rhi->clipSpaceCorrMatrix();
    transform.scale(scaleX, scaleY);
    updates.updateDynamicBuffer(m_transform.get(), 0, kTransformSize, transform.constData());
}

MonitorRenderer::Result MonitorRenderer::render(std::optional<VideoFrame> frame,
                                                std::expected<QImage, QString>* readback)
{
    if (frame && !frame->image.isNull()) {
        m_lastImage = std::move(frame->image);
        m_textureStale = true;
    }
    if (!m_window.isExposed())
        return Result::Skipped;
    if (!ensureDevice() || !ensureSwapChain())
        return Result::RetryLater;
    if (m_swapChain->surfacePixelSize().isEmpty())
        return Result::Skipped;
    if (m_swapChain->currentPixelSize() != m_swapChain->surfacePixelSize() && !m_swapChain->createOrResize())
        return Result::RetryLater;

    QRhi::FrameOpResult begun = m_rhi->beginFrame(m_swapChain.get());
    if (begun == QRhi::FrameOpSwapChainOutOfDate) {
        if (!m_swapChain->createOrResize())
            return Result::RetryLater;
        begun = m_rhi->beginFrame(m_swapChain.get());
    }
    if (begun == QRhi::FrameOpDeviceLost) {
        qCWarning(lcMonitor) << "The graphics device was lost; recreating the monitor renderer";
        releaseDevice();
        return Result::RetryLater;
    }
    if (begun != QRhi::FrameOpSuccess)
        return Result::RetryLater;

    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (!m_verticesUploaded) {
        updates->uploadStaticBuffer(m_vertices.get(), kQuad.data());
        m_verticesUploaded = true;
    }
    // A newer frame, or the last one again when the texture had to be recreated.
    bool uploadedNewFrame = false;
    if ((m_textureStale || !m_texture) && !m_lastImage.isNull()) {
        ensureTexture(m_lastImage.size());
        if (m_texture) {
            updates->uploadTexture(m_texture.get(), m_lastImage);
            uploadedNewFrame = m_textureStale;
            m_textureStale = false;
        }
    }
    const bool draw = m_texture && ensurePipeline();
    if (draw)
        updateTransform(*updates);

    QRhiCommandBuffer* commands = m_swapChain->currentFrameCommandBuffer();
    const QSize output = m_swapChain->currentPixelSize();
    commands->beginPass(m_swapChain->currentFrameRenderTarget(), Qt::black, {1.0f, 0}, updates);
    if (draw) {
        commands->setGraphicsPipeline(m_pipeline.get());
        commands->setViewport({0.0f, 0.0f, static_cast<float>(output.width()), static_cast<float>(output.height())});
        commands->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
        commands->setVertexInput(0, 1, &input);
        commands->draw(4);
    }
    QRhiReadbackResult readbackResult;
    QRhiResourceUpdateBatch* afterPass = nullptr;
    if (readback) {
        afterPass = m_rhi->nextResourceUpdateBatch();
        afterPass->readBackTexture(QRhiReadbackDescription(), &readbackResult);
    }
    commands->endPass(afterPass);
    m_rhi->endFrame(m_swapChain.get());

    if (uploadedNewFrame && draw)
        ++m_presented;
    if (readback) {
        m_rhi->finish();
        *readback = toImage(readbackResult);
    }
    return Result::Rendered;
}

std::expected<QImage, QString> MonitorRenderer::toImage(const QRhiReadbackResult& result) const
{
    QImage::Format format = QImage::Format_Invalid;
    if (result.format == QRhiTexture::BGRA8)
        format = QImage::Format_RGB32;
    else if (result.format == QRhiTexture::RGBA8)
        format = QImage::Format_RGBX8888;
    const QSize size = result.pixelSize;
    const qsizetype stride = static_cast<qsizetype>(size.width()) * 4;
    if (format == QImage::Format_Invalid || size.isEmpty() || result.data.size() < stride * size.height())
        return std::unexpected(
            u"The %1 monitor returned pixels in a format Mikser Studio cannot read back."_s.arg(backendName()));

    // The view borrows the readback buffer; copy before it goes away.
    const QImage view(reinterpret_cast<const uchar*>(result.data.constData()), size.width(), size.height(), stride,
                      format);
    QImage image = format == QImage::Format_RGB32 ? view.copy() : view.convertToFormat(QImage::Format_RGB32);
    if (m_rhi->isYUpInFramebuffer())
        image.flip(Qt::Vertical);
    return image;
}

void MonitorRenderer::releaseSwapChain()
{
    m_pipeline.reset();
    m_swapChain.reset();
    m_renderPass.reset();
}

void MonitorRenderer::releaseDevice()
{
    releaseSwapChain();
    m_bindings.reset();
    m_texture.reset();
    m_sampler.reset();
    m_transform.reset();
    m_vertices.reset();
    m_rhi.reset();
    m_frameSize = {};
}

}  // namespace mikserstudio::engine
