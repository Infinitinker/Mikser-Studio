#pragma once

#include <QObject>

#include <memory>

namespace mikserstudio::app {

// Turns Ctrl+C, Ctrl+Break and closing the console (Windows) or SIGINT, SIGTERM and SIGHUP
// (POSIX) into QCoreApplication::quit(), so exec() returns and everything is torn down normally:
// the API removes its discovery file, plugins are deactivated and the AI service is stopped. A
// second signal while Mikser Studio is quitting ends the process at once.
//
// Create one after the QCoreApplication and destroy it after everything that must be torn down
// (closing the console waits for that, up to the few seconds Windows allows). One at a time.
class ShutdownSignals final {
public:
    ShutdownSignals();
    ~ShutdownSignals();
    ShutdownSignals(const ShutdownSignals&) = delete;
    ShutdownSignals& operator=(const ShutdownSignals&) = delete;

private:
    std::unique_ptr<QObject> m_listener;  // POSIX: watches the signal handler's socket.
};

}  // namespace mikserstudio::app
