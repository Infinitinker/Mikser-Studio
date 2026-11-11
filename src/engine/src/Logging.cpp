#include "Logging.h"

namespace mikserstudio::engine {

Q_LOGGING_CATEGORY(lcEngine, "mikserstudio.engine")
// MLT and FFmpeg are very chatty below info level, so their debug output is opt-in.
Q_LOGGING_CATEGORY(lcMlt, "mikserstudio.engine.mlt", QtInfoMsg)
Q_LOGGING_CATEGORY(lcPlayback, "mikserstudio.engine.playback")
Q_LOGGING_CATEGORY(lcMonitor, "mikserstudio.engine.monitor")

}  // namespace mikserstudio::engine
