// Copies the picture of an MLT frame into a QImage for the monitor.
#pragma once

#include <framework/mlt.h>  // Declares the C API with C linkage.

#include <QImage>

namespace mikserstudio::engine {

// Copies the frame's picture into a Format_RGBA8888 image of the requested size (MLT scales
// when the consumer has not rendered it at that size yet). Null image when MLT fails.
[[nodiscard]] QImage frameToImage(mlt_frame frame, int width, int height);

}  // namespace mikserstudio::engine
