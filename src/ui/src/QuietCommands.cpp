// The quiet command scope and the test for quiet commands.
#include "QuietCommands.h"

#include "mikserstudio/core/CommandBus.h"

namespace mikserstudio::ui {

namespace {

int g_depth = 0;

}  // namespace

QuietCommandScope::QuietCommandScope()
{
    ++g_depth;
}

QuietCommandScope::~QuietCommandScope()
{
    --g_depth;
}

bool QuietCommandScope::active()
{
    return g_depth > 0;
}

bool isQuietCommand(const core::CommandBus& bus, const QString& id)
{
    if (QuietCommandScope::active())
        return true;
    const core::CommandSpec* spec = bus.spec(id);
    return spec && spec->readOnly;
}

}  // namespace mikserstudio::ui
