// The logging category of the MCP bridge, with its info messages on: MCP clients collect them from stderr.
#include "Logging.h"

namespace mikserstudio::mcp {

Q_LOGGING_CATEGORY(lcMcp, "mikserstudio.mcp", QtInfoMsg)

}  // namespace mikserstudio::mcp
