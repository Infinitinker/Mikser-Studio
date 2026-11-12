#include "ShutdownSignals.h"

#include "Logging.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string_view>
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

// `taskkill` without /F posts WM_CLOSE to one window of the process: the newest top-level window
// or, when there is none (headless), the newest hidden message-only window. Those hidden windows
// are the ones Qt's event dispatchers create, one per thread with an event loop (the main thread,
// Qt's network thread, ...), all of one window class. Their default handling destroys the window,
// which stops that thread's event loop for good, so the app hangs. Replacing the procedure of
// that class turns WM_CLOSE into a clean quit for every such window, including those of threads
// started later. The class name is a Qt implementation detail; tests/app checks it still matches.
constexpr std::wstring_view kQtDispatcherClassPrefix = L"QEventDispatcherWin32_Internal_Widget";

std::atomic<WNDPROC> g_qtDispatcherProc{nullptr};
std::atomic<bool> g_closeGuardActive{false};
std::atomic<bool> g_closeRequested{false};

// Runs on the thread that owns the window. Once ShutdownSignals is gone it only forwards.
LRESULT CALLBACK guardedDispatcherProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE && g_closeGuardActive.load()) {
        if (!g_closeRequested.exchange(true)) {
            qCInfo(lcApp) << "Another program asked Mikser Studio to close; shutting down.";
            requestQuit();
        }
        return 0;  // Never let the window destroy itself.
    }
    return CallWindowProcW(g_qtDispatcherProc.load(), window, message, wParam, lParam);
}

bool isQtDispatcherWindow(HWND window, DWORD threadId)
{
    DWORD processId = 0;
    const DWORD owner = GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || (threadId != 0 && owner != threadId))
        return false;
    wchar_t name[128] = {};
    return GetClassNameW(window, name, 128) > 0 && std::wstring_view(name).starts_with(kQtDispatcherClassPrefix);
}

// The hidden windows of Qt's event dispatchers in this process; only the calling thread's when
// `threadId` is not 0.
template <typename Fn> void forEachQtDispatcherWindow(DWORD threadId, Fn fn)
{
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr))) {
        if (isQtDispatcherWindow(window, threadId))
            fn(window);
    }
}

void installCloseGuard()
{
    // Qt creates the window with the dispatcher, i.e. with the application object. Never pump
    // events here: a WM_CLOSE already queued would reach Qt's own procedure before the guard.
    HWND own = nullptr;
    forEachQtDispatcherWindow(GetCurrentThreadId(), [&own](HWND window) { own = window; });
    if (!own) {
        qCWarning(lcApp) << "Closing Mikser Studio from another program (taskkill) may hang it: Qt's "
                            "event-loop window was not found";
        return;
    }
    const auto guarded = reinterpret_cast<LONG_PTR>(&guardedDispatcherProc);
    if (const ULONG_PTR classProc = GetClassLongPtrW(own, GCLP_WNDPROC); classProc != static_cast<ULONG_PTR>(guarded)) {
        g_qtDispatcherProc = reinterpret_cast<WNDPROC>(classProc);
        SetClassLongPtrW(own, GCLP_WNDPROC, guarded);  // Windows created from now on.
    }
    forEachQtDispatcherWindow(0, [guarded](HWND window) {  // Windows that already exist.
        if (GetWindowLongPtrW(window, GWLP_WNDPROC) != guarded)
            SetWindowLongPtrW(window, GWLP_WNDPROC, guarded);
    });
    g_closeRequested = false;
    g_closeGuardActive = true;
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
    installCloseGuard();
    if (!SetConsoleCtrlHandler(onConsoleEvent, TRUE))
        qCWarning(lcApp) << "Ctrl+C will end Mikser Studio without a clean shutdown: no console handler (error"
                         << GetLastError() << ")";
}

ShutdownSignals::~ShutdownSignals()
{
    g_closeGuardActive = false;
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
    struct sigaction action{};
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
