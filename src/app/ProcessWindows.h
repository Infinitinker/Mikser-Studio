// The windows of a process, which a close request from another program can reach (Windows only).
#pragma once

#include <QList>
#include <QString>

#include <windows.h>

namespace mikserstudio::app {

struct ProcessWindow {
    HWND handle = nullptr;
    QString className;
    DWORD threadId = 0;  // The thread that owns the window and handles its messages.
    bool messageOnly = false;  // A hidden message-only window rather than a top-level one.
};

// The windows of process `processId` as `taskkill` chooses among them: the top-level ones first,
// hidden ones included, then the message-only ones.
[[nodiscard]] QList<ProcessWindow> processWindows(DWORD processId);

[[nodiscard]] QString windowClassName(HWND window);

}  // namespace mikserstudio::app
