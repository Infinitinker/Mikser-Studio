// The engine demo program: plays one media file in a monitor panel.
#include "DemoWindow.h"
#include "mikserstudio/engine/ClipPlayer.h"
#include "mikserstudio/engine/MltRuntime.h"
#include "mikserstudio/engine/MonitorPanel.h"

#include "mikserstudio/core/Project.h"

#include <QApplication>
#include <QFileInfo>
#include <QTextStream>

using namespace Qt::StringLiterals;

namespace {

constexpr int kUsageError = 2;

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"mikser-studio-monitor-demo"_s);
    QTextStream err(stderr);

    const QStringList arguments = QApplication::arguments();
    if (arguments.size() != 2) {
        err << "Usage: mikser-studio-monitor-demo <file>\n";
        return kUsageError;
    }
    // The player takes only absolute paths (and colors); a relative one is meant from here.
    const QString& argument = arguments[1];
    const QString file = mikserstudio::core::isAllowedMediaPath(argument) ? argument
                                                                          : QFileInfo(argument).absoluteFilePath();

    const auto runtime = mikserstudio::engine::MltRuntime::initialize();
    if (!runtime) {
        err << runtime.error() << '\n';
        return kUsageError;
    }

    mikserstudio::engine::ClipPlayer player(**runtime, mikserstudio::engine::AudioOutput::Device);
    mikserstudio::demo::DemoWindow window(player, QApplication::translate("main", "Source monitor"));
    player.setFrameSink(window.monitorPanel()->frameSink());
    QObject::connect(&player, &mikserstudio::engine::MltPlayer::errorOccurred, &app,
                     [&err](const QString& message) { err << message << Qt::endl; });

    if (auto opened = player.openFile(file); !opened) {
        err << opened.error() << '\n';
        return 1;
    }
    window.show();
    return QApplication::exec();
}
