#pragma once

#include "mikserstudio/core/Project.h"

#include <memory>

namespace Mlt {
class Profile;
}

namespace mikserstudio::engine {

// A progressive, square-pixel, BT.709 MLT profile of the given rate and size.
[[nodiscard]] std::unique_ptr<Mlt::Profile> makeProfile(core::Rational frameRate, int width, int height);

[[nodiscard]] bool profileMatches(Mlt::Profile& profile, core::Rational frameRate, int width, int height);

}  // namespace mikserstudio::engine
