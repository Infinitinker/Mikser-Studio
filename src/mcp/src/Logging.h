// The logging category of the MCP bridge. Its log goes to stderr, since stdout carries the protocol.
#pragma once

#include <QLoggingCategory>

namespace mikserstudio::mcp {

Q_DECLARE_LOGGING_CATEGORY(lcMcp)

}  // namespace mikserstudio::mcp
