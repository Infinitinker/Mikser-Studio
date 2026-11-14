// Listing the windows of a process: top-level ones through EnumWindows, message-only ones through FindWindowEx.
#include "ProcessWindows.h"

namespace mikserstudio::app {

namespace {

struct WindowSearch {
    DWORD processId = 0;
    QList<ProcessWindow> found;
};

void addIfOwned(HWND window, bool messageOnly, WindowSearch& search)
{
    DWORD owner = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &owner);
    if (owner == search.processId)
        search.found.append(ProcessWindow{window, windowClassName(window), threadId, messageOnly});
}

}  // namespace

QList<ProcessWindow> processWindows(DWORD processId)
{
    WindowSearch search{.processId = processId, .found = {}};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            addIfOwned(window, false, *reinterpret_cast<WindowSearch*>(data));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    HWND window = nullptr;
    while ((window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr)))
        addIfOwned(window, true, search);
    return search.found;
}

QString windowClassName(HWND window)
{
    wchar_t name[256] = {};
    const int length = GetClassNameW(window, name, 256);
    return QString::fromWCharArray(name, length);
}

}  // namespace mikserstudio::app
