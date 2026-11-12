#include "ShutdownSignals.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>

#include <string_view>
#endif

using mikserstudio::app::ShutdownSignals;

#ifdef Q_OS_WIN

namespace {

// The hidden, message-only window Qt's event dispatcher uses on the given thread.
HWND eventLoopWindow(DWORD threadId)
{
    constexpr std::wstring_view prefix = L"QEventDispatcherWin32_Internal_Widget";
    HWND found = nullptr;
    while ((found = FindWindowExW(HWND_MESSAGE, found, nullptr, nullptr))) {
        DWORD pid = 0;
        if (GetWindowThreadProcessId(found, &pid) != threadId || pid != GetCurrentProcessId())
            continue;
        wchar_t name[128] = {};
        if (GetClassNameW(found, name, 128) > 0 && std::wstring_view(name).starts_with(prefix))
            return found;
    }
    return nullptr;
}

// A thread with its own event loop, like Qt's network thread.
class EventLoopThread {
public:
    EventLoopThread()
    {
        m_context->moveToThread(&m_thread);
        m_thread.start();
        QSemaphore started;
        QMetaObject::invokeMethod(m_context.get(), [this, &started] {
            m_threadId = GetCurrentThreadId();
            started.release();
        });
        started.acquire();
    }
    ~EventLoopThread()
    {
        m_thread.quit();
        m_thread.wait();
    }
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    [[nodiscard]] HWND window() const { return eventLoopWindow(m_threadId); }

    // False if the thread's event loop no longer runs.
    [[nodiscard]] bool answers()
    {
        // Shared: after a timeout the queued call may still run on a slow thread.
        auto answered = std::make_shared<QSemaphore>();
        QMetaObject::invokeMethod(m_context.get(), [answered] { answered->release(); });
        return answered->tryAcquire(1, 5'000);
    }

private:
    QThread m_thread;
    std::unique_ptr<QObject> m_context = std::make_unique<QObject>();
    DWORD m_threadId = 0;
};

// What `taskkill` without /F does, then runs the main event loop: true if that asked to quit.
// Before the fix the window destroyed itself and nothing quit.
bool closeRequestQuits(HWND window)
{
    if (!PostMessageW(window, WM_CLOSE, 0, 0))
        return false;
    QTimer giveUp;
    giveUp.setSingleShot(true);
    QObject::connect(&giveUp, &QTimer::timeout, [] { QCoreApplication::exit(2); });
    giveUp.start(10'000);
    return QCoreApplication::exec() == 0;
}

}  // namespace

TEST_CASE("A close request from another program quits cleanly on any thread's event-loop window", "[app][shutdown]")
{
    // Sections rerun this from the top; the first one is the run where `before` really predates
    // the guard, so it comes first.
    EventLoopThread before;
    const ShutdownSignals shutdown;
    EventLoopThread after;  // Created with the guard in place, like Qt's network thread.

    SECTION("a thread that was already running")
    {
        const HWND window = before.window();
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));
        CHECK(before.answers());
    }
    SECTION("a thread started later")
    {
        const HWND window = after.window();
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));
        CHECK(after.answers());
    }
    SECTION("the main thread")
    {
        const HWND window = eventLoopWindow(GetCurrentThreadId());
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));

        // A second request while quitting goes through the guard too (sent, so it is handled
        // right here): the window survives and no second quit is queued.
        SendMessageW(window, WM_CLOSE, 0, 0);
        CHECK(IsWindow(window));
        QTimer::singleShot(50, [] { QCoreApplication::exit(7); });
        CHECK(QCoreApplication::exec() == 7);
        CHECK(IsWindow(window));
    }
}

#else

TEST_CASE("Shutdown signals install and uninstall", "[app][shutdown]")
{
    const ShutdownSignals shutdown;
    SUCCEED();
}

#endif
