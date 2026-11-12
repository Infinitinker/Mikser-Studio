// Copying an MLT frame's picture into a QImage.
#include "FrameConversion.h"

#include <framework/mlt.h>  // Declares the C API with C linkage.

#include <cstring>
#include <utility>

namespace mikserstudio::engine {

QImage frameToImage(mlt_frame frame, int width, int height)
{
    mlt_image_format format = mlt_image_rgba;
    int frameWidth = width;
    int frameHeight = height;
    std::uint8_t* data = nullptr;
    if (mlt_frame_get_image(frame, &data, &format, &frameWidth, &frameHeight, 0) != 0 || !data ||
        format != mlt_image_rgba || frameWidth <= 0 || frameHeight <= 0)
        return {};

    QImage image(frameWidth, frameHeight, QImage::Format_RGBA8888);
    if (image.isNull())
        return {};
    const auto rowBytes = static_cast<std::size_t>(frameWidth) * 4;
    if (std::cmp_equal(image.bytesPerLine(), rowBytes)) {
        std::memcpy(image.bits(), data, rowBytes * static_cast<std::size_t>(frameHeight));
    } else {
        for (int y = 0; y < frameHeight; ++y)
            std::memcpy(image.scanLine(y), data + rowBytes * static_cast<std::size_t>(y), rowBytes);
    }
    return image;
}

}  // namespace mikserstudio::engine
