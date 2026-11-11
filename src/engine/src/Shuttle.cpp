#include "Shuttle.h"

#include <algorithm>

namespace mikserstudio::engine {

namespace {

constexpr double kMaxShuttleSpeed = 8.0;

}  // namespace

double nextShuttleSpeed(double currentSpeed, ShuttleKey key)
{
    switch (key) {
    case ShuttleKey::Stop:
        return 0.0;
    case ShuttleKey::Forward:
        return currentSpeed >= 1.0 ? std::min(currentSpeed * 2.0, kMaxShuttleSpeed) : 1.0;
    case ShuttleKey::Reverse:
        return currentSpeed <= -1.0 ? std::max(currentSpeed * 2.0, -kMaxShuttleSpeed) : -1.0;
    }
    return 0.0;
}

}  // namespace mikserstudio::engine
