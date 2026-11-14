// The app.info command: which Mikser Studio runs, and its version.
#pragma once

namespace mikserstudio::core {

class CommandBus;

// app.info {} -> {name, version} (readOnly): the application's name, which names the instance
// ("Mikser Studio", "Mikser Studio Dev" or "Mikser Studio E2E", see Instance.h), and its version.
void registerAppInfoCommand(CommandBus& bus);

}  // namespace mikserstudio::core
