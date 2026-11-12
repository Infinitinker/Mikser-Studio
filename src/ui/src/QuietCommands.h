// Tells which commands the command log and the status bar leave out: read-only ones and UI bookkeeping.
#pragma once

#include <QString>

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// The command log and the status bar leave out quiet commands: read-only ones
// (CommandSpec::readOnly, e.g. the "ai.jobs" polls or "program.status") and those the UI runs
// inside a QuietCommandScope, such as the many seeks of scrubbing the playhead. Commands run on
// the main thread and CommandBus signals are emitted synchronously, so a scope covers exactly the
// commands executed while it lives.
class QuietCommandScope {
public:
    QuietCommandScope();
    ~QuietCommandScope();
    QuietCommandScope(const QuietCommandScope&) = delete;
    QuietCommandScope& operator=(const QuietCommandScope&) = delete;

    [[nodiscard]] static bool active();
};

[[nodiscard]] bool isQuietCommand(const core::CommandBus& bus, const QString& id);

}  // namespace mikserstudio::ui
