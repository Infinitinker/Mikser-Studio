// The desktop front end: the main window (the ui module's shell) with the engine's monitor panels, and the
// start-up options that import, play or take a screenshot.
#pragma once

#include <QString>
#include <QStringList>

#include <memory>

namespace mikserstudio::ui {
class Shell;
}

namespace mikserstudio::app {

class Studio;

struct DesktopOptions {
    QStringList importFiles;  // Imported and appended to track V1 after start.
    bool play = false;  // Start program playback once the window is up.
    QString screenshotPath;  // Save a screenshot of the main window, then quit.
    int screenshotDelayMs = 4000;
};

// The desktop front end: the Shell (docking, panels, menus) plus the engine's monitor panels.
class DesktopApp final {
public:
    DesktopApp(Studio& studio, const QString& configDir, DesktopOptions options);
    ~DesktopApp();
    DesktopApp(const DesktopApp&) = delete;
    DesktopApp& operator=(const DesktopApp&) = delete;

    void show();

private:
    void registerMonitors();
    void importStartupFiles();
    void scheduleScreenshot();

    Studio& m_studio;
    DesktopOptions m_options;
    std::unique_ptr<ui::Shell> m_shell;
};

}  // namespace mikserstudio::app
