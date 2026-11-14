// Building the desktop front end: registering the monitors as panels, importing files given at start-up and
// saving a screenshot of the main window.
#include "DesktopApp.h"

#include "Logging.h"
#include "Studio.h"

#include "mikserstudio/engine/MonitorPanel.h"
#include "mikserstudio/ui/DockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Shell.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

void registerMonitor(ui::DockHost& host, QStringView id, const QString& title, engine::MltPlayer& player)
{
    host.registerPanel({id.toString(), title, ui::DockArea::Center, true, [&player](QWidget* parent) {
                            auto* panel = new engine::MonitorPanel(player, parent);
                            player.setFrameSink(panel->frameSink());
                            return panel;
                        }});
}

// Grabs the screen pixels under the window, so the GPU monitors (native child windows) are included.
bool saveWindowScreenshot(QWidget* window, const QString& path)
{
    QScreen* screen = window->screen();
    if (!screen)
        return false;
    const QRect frame = window->frameGeometry();
    const QPoint origin = screen->geometry().topLeft();
    const QPixmap pixels = screen->grabWindow(0, frame.x() - origin.x(), frame.y() - origin.y(), frame.width(),
                                              frame.height());
    return pixels.save(path);
}

}  // namespace

DesktopApp::DesktopApp(Studio& studio, const QString& configDir, DesktopOptions options)
    : m_studio(studio)
    , m_options(std::move(options))
    , m_shell(std::make_unique<ui::Shell>(studio.bus(), studio.project(), studio.plugins(), configDir))
{
    registerMonitors();
    if (engine::TimelinePlayer* program = studio.programPlayer())
        m_shell->setProgramTransport(program);
}

DesktopApp::~DesktopApp() = default;

void DesktopApp::registerMonitors()
{
    ui::DockHost& host = m_shell->dockHost();
    if (engine::ClipPlayer* source = m_studio.sourcePlayer())
        registerMonitor(host, ui::panel_ids::kSourceMonitor, u"Source monitor"_s, *source);
    if (engine::TimelinePlayer* program = m_studio.programPlayer())
        registerMonitor(host, ui::panel_ids::kProgramMonitor, u"Program monitor"_s, *program);

    for (engine::MltPlayer* player : {static_cast<engine::MltPlayer*>(m_studio.sourcePlayer()),
                                      static_cast<engine::MltPlayer*>(m_studio.programPlayer())}) {
        if (player)
            QObject::connect(player, &engine::MltPlayer::errorOccurred, m_shell->mainWindow(),
                             [](const QString& message) { qCWarning(lcApp) << "Playback:" << message; });
    }
}

void DesktopApp::show()
{
    m_shell->restoreState();
    m_shell->show();

    if (const QString error = m_studio.engineError(); !error.isEmpty()) {
        QMessageBox::warning(m_shell->mainWindow(), u"Playback is unavailable"_s,
                             u"Mikser Studio could not start its playback engine, so media cannot be imported or "
                             u"played.\n\n%1"_s.arg(error));
    }
    importStartupFiles();
    if (m_options.play) {
        if (engine::TimelinePlayer* program = m_studio.programPlayer())
            QTimer::singleShot(500, m_shell->mainWindow(), [program] { program->play(); });
    }
    if (!m_options.screenshotPath.isEmpty())
        scheduleScreenshot();
}

void DesktopApp::importStartupFiles()
{
    core::CommandBus& bus = m_studio.bus();
    bool first = true;
    for (const QString& file : m_options.importFiles) {
        const QString path = QDir::fromNativeSeparators(QFileInfo(file).absoluteFilePath());
        const auto imported = bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, path}});
        if (!imported) {
            qCWarning(lcApp) << "Could not import" << path << ":" << imported.error().message;
            continue;
        }
        const QString mediaId = imported->value(u"mediaId"_s).toString();
        const qint64 end = m_studio.project().duration();
        const auto placed = bus.execute(
            u"timeline.add_clip"_s,
            QJsonObject{{u"mediaId"_s, mediaId}, {u"trackId"_s, u"V1"_s}, {u"position"_s, end}});
        if (!placed)
            qCWarning(lcApp) << "Could not place" << path << "on the timeline:" << placed.error().message;
        if (first && bus.contains(u"source.open_media"_s)) {
            (void)bus.execute(u"source.open_media"_s, QJsonObject{{u"mediaId"_s, mediaId}});
            first = false;
        }
    }
}

void DesktopApp::scheduleScreenshot()
{
    QMainWindow* window = m_shell->mainWindow();
    QTimer::singleShot(m_options.screenshotDelayMs, window, [this, window] {
        const QString path = QFileInfo(m_options.screenshotPath).absoluteFilePath();
        const bool saved = saveWindowScreenshot(window, path);
        qCInfo(lcApp) << (saved ? "Saved screenshot" : "Could not save screenshot") << path;
        m_shell->saveState();
        QApplication::exit(saved ? 0 : 1);
    });
}

}  // namespace mikserstudio::app
