#include "ShutdownSignals.h"

#include "Logging.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#else
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>
#endif

namespace mikserstudio::app {

namespace {

void requestQuit()
{
    // Queued: the request may come from another thread, or before exec() runs.
    QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
}

}  // namespace

#ifdef Q_OS_WIN

namespace {

// Windows ends the process as soon as the handler returns from these events.
constexpr std::chrono::milliseconds kCloseWait{4500};

struct ConsoleState {
    std::mutex mutex;
    std::condition_variable tornDown;
    bool installed = false;
    bool quitting = false;
};

ConsoleState& consoleState()
{
    static ConsoleState state;
    return state;
}

// Runs on a thread the system creates for each console event.
BOOL WINAPI onConsoleEvent(DWORD event)
{
    ConsoleState& state = consoleState();
    std::unique_lock lock(state.mutex);
    if (!state.installed || state.quitting)
        return FALSE;  // The default handler ends the process.
    state.quitting = true;
    requestQuit();
    if (event == CTRL_CLOSE_EVENT || event == CTRL_LOGOFF_EVENT || event == CTRL_SHUTDOWN_EVENT)
        state.tornDown.wait_for(lock, kCloseWait, [&state] { return !state.installed; });
    return TRUE;
}

}  // namespace

ShutdownSignals::ShutdownSignals()
{
    ConsoleState& state = consoleState();
    {
        const std::lock_guard lock(state.mutex);
        state.installed = true;
        state.quitting = false;
    }
    if (!SetConsoleCtrlHandler(onConsoleEvent, TRUE))
        qCWarning(lcApp) << "Ctrl+C will end Mikser Studio without a clean shutdown: no console handler (error"
                         << GetLastError() << ")";
}

ShutdownSignals::~ShutdownSignals()
{
    ConsoleState& state = consoleState();
    {
        const std::lock_guard lock(state.mutex);
        state.installed = false;
    }
    state.tornDown.notify_all();
    SetConsoleCtrlHandler(onConsoleEvent, FALSE);
}

#else

namespace {

constexpr int kSignals[] = {SIGINT, SIGTERM, SIGHUP};

// The handler may only do async-signal-safe things: it writes a byte that the event loop reads.
int g_signalSockets[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(g_signalSockets[1], &byte, 1);
}

void setHandler(void (*handler)(int))
{
    struct sigaction action {};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int number : kSignals)
        sigaction(number, &action, nullptr);
}

}  // namespace

ShutdownSignals::ShutdownSignals()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalSockets) != 0) {
        qCWarning(lcApp) << "Signals will end Mikser Studio without a clean shutdown: no socket pair for them";
        return;
    }
    auto* notifier = new QSocketNotifier(g_signalSockets[0], QSocketNotifier::Read);
    m_listener.reset(notifier);
    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [notifier] {
        char byte = 0;
        [[maybe_unused]] const auto drained = ::read(g_signalSockets[0], &byte, 1);
        notifier->setEnabled(false);
        setHandler(SIG_DFL);  // A second signal ends the process at once.
        requestQuit();
    });
    setHandler(onSignal);
}

ShutdownSignals::~ShutdownSignals()
{
    if (!m_listener)
        return;
    setHandler(SIG_DFL);
    m_listener.reset();
    for (int& socket : g_signalSockets)
        ::close(std::exchange(socket, -1));
}

#endif

}  // namespace mikserstudio::app
