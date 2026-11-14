// Choosing the QRhi backend of the monitor windows, preparing their surfaces and creating the QRhi.
#include "RhiBackend.h"

#include "Logging.h"

#include <rhi/qrhi_platform.h>

#include <QGuiApplication>
#include <QWindow>

#include <optional>

#if QT_CONFIG(opengl)
#include <QOffscreenSurface>
#endif
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

#if QT_CONFIG(vulkan)
QVulkanInstance* vulkanInstance()
{
    // Shared by all monitor windows; must outlive them, so it lives until the process exits.
    static QVulkanInstance* instance = [] {
        auto* created = new QVulkanInstance;
        created->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!created->create()) {
            delete created;
            return static_cast<QVulkanInstance*>(nullptr);
        }
        return created;
    }();
    return instance;
}
#endif

std::optional<QRhi::Implementation> fromEnvironment()
{
    const QString name = qEnvironmentVariable("MIKSERSTUDIO_MONITOR_RHI").toLower();
    if (name.isEmpty())
        return std::nullopt;
    if (name == u"null"_s)
        return QRhi::Null;
    if (name == u"d3d11"_s)
        return QRhi::D3D11;
    if (name == u"d3d12"_s)
        return QRhi::D3D12;
    if (name == u"metal"_s)
        return QRhi::Metal;
    if (name == u"vulkan"_s)
        return QRhi::Vulkan;
    if (name == u"opengl"_s || name == u"gl"_s)
        return QRhi::OpenGLES2;
    qCWarning(lcMonitor).noquote() << "Ignoring unknown MIKSERSTUDIO_MONITOR_RHI value" << name;
    return std::nullopt;
}

}  // namespace

QRhi::Implementation preferredRhiBackend()
{
    if (const auto requested = fromEnvironment())
        return *requested;
    const QString platform = QGuiApplication::platformName();
    if (platform == u"offscreen"_s || platform == u"minimal"_s)
        return QRhi::Null;
#ifdef Q_OS_WIN
    return QRhi::D3D11;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return QRhi::Metal;
#else
#if QT_CONFIG(vulkan)
    if (vulkanInstance())
        return QRhi::Vulkan;
#endif
    return QRhi::OpenGLES2;
#endif
}

QSurface::SurfaceType surfaceTypeFor(QRhi::Implementation backend)
{
    switch (backend) {
    case QRhi::D3D11:
    case QRhi::D3D12:
        return QSurface::Direct3DSurface;
    case QRhi::Metal:
        return QSurface::MetalSurface;
    case QRhi::Vulkan:
        return QSurface::VulkanSurface;
    case QRhi::OpenGLES2:
        return QSurface::OpenGLSurface;
    case QRhi::Null:
        break;
    }
    return QSurface::RasterSurface;
}

void prepareWindow(QWindow& window, QRhi::Implementation backend)
{
    window.setSurfaceType(surfaceTypeFor(backend));
#if QT_CONFIG(vulkan)
    if (backend == QRhi::Vulkan)
        window.setVulkanInstance(vulkanInstance());
#endif
}

std::unique_ptr<QRhi> createRhi(QRhi::Implementation backend, QWindow& window)
{
    std::unique_ptr<QRhi> rhi;
    switch (backend) {
#ifdef Q_OS_WIN
    case QRhi::D3D11: {
        QRhiD3D11InitParams params;
        rhi.reset(QRhi::create(QRhi::D3D11, &params));
        break;
    }
    case QRhi::D3D12: {
        QRhiD3D12InitParams params;
        rhi.reset(QRhi::create(QRhi::D3D12, &params));
        break;
    }
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    case QRhi::Metal: {
        QRhiMetalInitParams params;
        rhi.reset(QRhi::create(QRhi::Metal, &params));
        break;
    }
#endif
#if QT_CONFIG(vulkan)
    case QRhi::Vulkan: {
        QRhiVulkanInitParams params;
        params.inst = window.vulkanInstance();
        params.window = &window;
        if (params.inst)
            rhi.reset(QRhi::create(QRhi::Vulkan, &params));
        break;
    }
#endif
#if QT_CONFIG(opengl)
    case QRhi::OpenGLES2: {
        // Owned by the application: QRhi needs it for as long as it exists, and monitor windows
        // can come and go many times.
        static QOffscreenSurface* fallbackSurface = QRhiGles2InitParams::newFallbackSurface();
        QRhiGles2InitParams params;
        params.fallbackSurface = fallbackSurface;
        params.window = &window;
        rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
        break;
    }
#endif
    default:
        break;
    }

    if (!rhi && backend != QRhi::Null) {
        qCWarning(lcMonitor).noquote() << "Could not create a" << QRhi::backendName(backend)
                                       << "device for the monitor; video will not be shown (Null backend)";
    }
    if (!rhi) {
        QRhiNullInitParams params;
        rhi.reset(QRhi::create(QRhi::Null, &params));
    }
    return rhi;
}

}  // namespace mikserstudio::engine
