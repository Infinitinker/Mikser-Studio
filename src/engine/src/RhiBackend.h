#pragma once

#include <rhi/qrhi.h>

#include <QSurface>

#include <memory>

class QWindow;

namespace mikserstudio::engine {

// The QRhi backend for monitor windows: $MIKSERSTUDIO_MONITOR_RHI (d3d11, d3d12, metal, vulkan, opengl,
// null) if set, Null on the offscreen/minimal platforms, otherwise D3D11 on Windows, Metal on
// macOS, Vulkan (when a Vulkan instance can be created) or OpenGL elsewhere.
[[nodiscard]] QRhi::Implementation preferredRhiBackend();

// The QWindow surface type a backend draws to. Set it before the window is created.
[[nodiscard]] QSurface::SurfaceType surfaceTypeFor(QRhi::Implementation backend);

// Prepares `window` for the backend (e.g. attaches the Vulkan instance).
void prepareWindow(QWindow& window, QRhi::Implementation backend);

// Creates the QRhi; falls back to the Null backend (logging why) when the device cannot be made.
[[nodiscard]] std::unique_ptr<QRhi> createRhi(QRhi::Implementation backend, QWindow& window);

}  // namespace mikserstudio::engine
