// Turns Ctrl+C, closing the console and termination signals into a normal quit, so everything is torn down in
// order.
#pragma once

#include <QObject>

#include <memory>

namespace mikserstudio::app {

// Turns Ctrl+C, Ctrl+Break and closing the console (Windows) or SIGINT, SIGTERM and SIGHUP
// (POSIX) into QCoreApplication::quit(), so exec() returns and everything is torn down normally:
// the API removes its discovery file, plugins are deactivated and running AI jobs are canceled. A
// second signal while Mikser Studio is quitting ends the process at once.
//
// On Windows it also turns a close request from another program (`taskkill` without /F posts
// WM_CLOSE to one window of the process) into quit(), whichever window of the main thread or of a
// thread with a Qt event loop it reaches. The window stays: Qt's hidden event-loop windows would
// otherwise destroy themselves and leave the app hanging. Without an interface it keeps a hidden
// top-level window on the main thread, so the request never goes to a library's window. It logs
// the windows such a request can reach, once installed and again when the event loop starts,
// marking those where it would be lost.
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
