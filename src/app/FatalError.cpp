// Start-up errors: in the log, on stderr or the terminal Mikser Studio was started from, or in a box.
#include "FatalError.h"

#include "Logging.h"

#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>

#include <cstdio>
#include <string>
#endif

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

#ifdef Q_OS_WIN
// Writes the message to stderr when it goes somewhere, else to the console of the process that started
// this one. False when there is neither.
bool writeToConsole(const QString& message)
{
    HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    const bool hasStderr = handle != nullptr && handle != INVALID_HANDLE_VALUE &&
                           GetFileType(handle) != FILE_TYPE_UNKNOWN;
    if (!hasStderr) {
        FILE* console = nullptr;
        if (AttachConsole(ATTACH_PARENT_PROCESS) == 0 || freopen_s(&console, "CONOUT$", "w", stderr) != 0)
            return false;
    }
    std::fprintf(stderr, "%s\n", qPrintable(message));
    std::fflush(stderr);
    return true;
}
#endif

}  // namespace

void reportFatalError(const QString& message, StartedAs startedAs)
{
    qCCritical(lcApp).noquote() << message;
#ifdef Q_OS_WIN
    // Qt writes its messages to the console only when the process has one; otherwise to the debugger.
    const bool shownByQt = GetConsoleWindow() != nullptr;
    const bool written = shownByQt || writeToConsole(message);
    if (startedAs == StartedAs::Headless) {
        if (!written) {
            const std::wstring text = message.toStdWString();
            MessageBoxW(nullptr, text.c_str(), L"Mikser Studio", MB_OK | MB_ICONERROR);
        }
        return;
    }
#endif
    if (startedAs == StartedAs::Desktop)
        QMessageBox::critical(nullptr, u"Mikser Studio"_s, message);
}

}  // namespace mikserstudio::app
