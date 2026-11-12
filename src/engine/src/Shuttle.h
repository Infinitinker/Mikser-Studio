// The J/K/L shuttle of the monitors: the playback speed after each key press.
#pragma once

namespace mikserstudio::engine {

enum class ShuttleKey { Reverse, Stop, Forward };  // J, K, L

// The J/K/L shuttle: L plays forward and doubles the speed on every repeat (1, 2, 4, 8), J does
// the same in reverse, K stops. Pressing the opposite direction starts again at 1x that way.
[[nodiscard]] double nextShuttleSpeed(double currentSpeed, ShuttleKey key);

}  // namespace mikserstudio::engine
