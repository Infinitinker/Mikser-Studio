// Routes MLT's own log output into the engine's logging category.
#pragma once

namespace mikserstudio::engine {

// Routes MLT's log output into the "mikserstudio.engine.mlt" logging category. Errors become warnings,
// MLT warnings become info messages, everything more verbose becomes debug messages.
void installMltLogHandler();

}  // namespace mikserstudio::engine
