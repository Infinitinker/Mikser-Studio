#include "MltProfile.h"

#include <Mlt.h>

#include <numeric>

namespace mikserstudio::engine {

std::unique_ptr<Mlt::Profile> makeProfile(core::Rational frameRate, int width, int height)
{
    auto profile = std::make_unique<Mlt::Profile>();
    const int divisor = std::max(1, std::gcd(width, height));
    profile->set_frame_rate(frameRate.num, frameRate.den);
    profile->set_width(width);
    profile->set_height(height);
    profile->set_sample_aspect(1, 1);
    profile->set_display_aspect(width / divisor, height / divisor);
    profile->set_progressive(1);
    profile->set_colorspace(709);
    profile->set_explicit(1);
    return profile;
}

bool profileMatches(Mlt::Profile& profile, core::Rational frameRate, int width, int height)
{
    return profile.frame_rate_num() == frameRate.num && profile.frame_rate_den() == frameRate.den
           && profile.width() == width && profile.height() == height;
}

}  // namespace mikserstudio::engine
