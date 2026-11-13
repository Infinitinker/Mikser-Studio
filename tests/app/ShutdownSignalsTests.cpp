// Shutdown signals: a close request from another program quits the app cleanly, whichever window it
// reaches (an event-loop window of any thread, or another window of the main thread), the windows it
// can reach are logged, and the handlers install and uninstall.
#include "ShutdownSignals.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <memory>

#ifdef Q_OS_WIN
#include "ProcessWindows.h"

#include <QMutex>
#include <QRegularExpression>
#include <QStringList>

#include <windows.h>

#include <thread>
#endif

using namespace Qt::StringLiterals;
using mikserstudio::app::ShutdownSignals;

#ifdef Q_OS_WIN

namespace {

// The hidden, message-only window Qt's event dispatcher uses on the given thread.
HWND eventLoopWindow(DWORD threadId)
{
    for (const mikserstudio::app::ProcessWindow& window : mikserstudio::app::processWindows(GetCurrentProcessId())) {
        if (window.messageOnly && window.threadId == threadId &&
            window.className.startsWith(u"QEventDispatcherWin32_Internal_Widget"_s)) {
            return window.handle;
        }
    }
    return nullptr;
}

// A thread with a plain Win32 window and no Qt event loop, as a library might start: nothing turns
// a close request that reaches its window into a quit.
class PlainWindowThread {
public:
    PlainWindowThread()
        : m_thread([this] {
            m_threadId = GetCurrentThreadId();
            HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
            m_created.release();
            m_done.acquire();
            DestroyWindow(window);
        })
    {
        m_created.acquire();
    }
    ~PlainWindowThread()
    {
        m_done.release();
        m_thread.join();
    }
    PlainWindowThread(const PlainWindowThread&) = delete;
    PlainWindowThread& operator=(const PlainWindowThread&) = delete;

    [[nodiscard]] DWORD threadId() const { return m_threadId; }

private:
    QSemaphore m_created;
    QSemaphore m_done;
    DWORD m_threadId = 0;
    std::thread m_thread;
};

// The messages logged on the mikserstudio.app category while it lives.
class AppLog {
public:
    AppLog() { m_previous = qInstallMessageHandler(&AppLog::handle); }
    ~AppLog()
    {
        qInstallMessageHandler(m_previous);
        const QMutexLocker lock(&mutex());
        messages().clear();
    }
    AppLog(const AppLog&) = delete;
    AppLog& operator=(const AppLog&) = delete;

    [[nodiscard]] static QStringList lines()
    {
        const QMutexLocker lock(&mutex());
        return messages();
    }

private:
    static QMutex& mutex()
    {
        static QMutex instance;
        return instance;
    }
    static QStringList& messages()
    {
        static QStringList instance;
        return instance;
    }
    static void handle(QtMsgType, const QMessageLogContext& context, const QString& message)
    {
        if (context.category != nullptr && qstrcmp(context.category, "mikserstudio.app") == 0) {
            const QMutexLocker lock(&mutex());
            messages().append(message);
        }
    }

    QtMessageHandler m_previous = nullptr;
};

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

TEST_CASE("A close request from another program quits cleanly, whichever window it reaches", "[app][shutdown]")
{
    // Sections rerun this from the top; the first one is the run where `before` really predates
    // the guard, so it comes first.
    EventLoopThread before;
    const ShutdownSignals shutdown;
    EventLoopThread after;  // Created with the guard in place, like Qt's network thread.

    SECTION("a thread that was already running")
    {
        HWND window = before.window();
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));
        CHECK(before.answers());
    }
    SECTION("a thread started later")
    {
        HWND window = after.window();
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));
        CHECK(after.answers());
    }
    SECTION("the main thread")
    {
        HWND window = eventLoopWindow(GetCurrentThreadId());
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
    SECTION("another window of the main thread")
    {
        // A hidden top-level window, such as a library makes: `taskkill` prefers it to Qt's
        // message-only one. A STATIC control would destroy itself on WM_CLOSE.
        HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
        REQUIRE(window != nullptr);
        CHECK(closeRequestQuits(window));
        CHECK(IsWindow(window));
        DestroyWindow(window);
    }
}

TEST_CASE("The windows a close request can reach are logged, with those no handler covers", "[app][shutdown]")
{
    const PlainWindowThread plain;
    const AppLog log;
    {
        const ShutdownSignals shutdown;
    }
    const QStringList lines = AppLog::lines().filter(u"(taskkill)"_s);
    REQUIRE(lines.size() == 1);
    const QString& line = lines.first();
    INFO(line.toStdString());
    CHECK(line.contains(u"is ignored if the request reaches a window marked \"not covered\""_s));
    const QRegularExpression mainDispatcher(
        uR"(message-only QEventDispatcherWin32_Internal_Widget\d+ \(main thread\))"_s);
    CHECK(line.contains(mainDispatcher));
    CHECK(line.contains(u"top-level Static (thread %1, not covered)"_s.arg(plain.threadId())));
}

#else

TEST_CASE("Shutdown signals install and uninstall", "[app][shutdown]")
{
    const ShutdownSignals shutdown;
    SUCCEED();
}

#endif
