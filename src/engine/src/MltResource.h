#pragma once

#include <QString>

#include <expected>
#include <memory>

namespace Mlt {
class Producer;
class Profile;
}  // namespace Mlt

namespace mikserstudio::engine {

// True for the MLT colour resources Mikser Studio accepts ("color:#ff0000", "color:red"; see
// core::isAllowedMediaPath). It is the only MLT service string taken from project or user data.
[[nodiscard]] bool isColourResource(const QString& path);

// Opens an absolute local media file or a colour resource with MLT's loader (which adds the
// normalising filters). Anything else is refused before MLT sees it: URLs, network shares, other
// MLT service strings ("xml-string:...", "avformat:...") and MLT/Kdenlive documents, which make
// MLT open further resources of their own. Fails with a user-facing message when the file is
// missing, not allowed or unreadable.
[[nodiscard]] std::expected<std::unique_ptr<Mlt::Producer>, QString> openProducer(Mlt::Profile& profile,
                                                                                  const QString& path);

}  // namespace mikserstudio::engine
