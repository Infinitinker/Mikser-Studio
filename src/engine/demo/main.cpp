#include "DemoWindow.h"
#include "SelfTest.h"
#include "mikserstudio/engine/ClipPlayer.h"
#include "mikserstudio/engine/MltRuntime.h"
#include "mikserstudio/engine/MonitorPanel.h"

#include "mikserstudio/core/Project.h"

#include <QApplication>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int kUsageError = 2;

struct Options {
    QString file;
    bool selfTest = false;
    std::chrono::milliseconds duration{0};
    QString output;
};

std::optional<Options> parse(const QStringList& arguments)
{
    Options options;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (arguments[i] == u"--self-test"_s) {
            if (i + 2 >= arguments.size())
                return std::nullopt;
            bool ok = false;
            const double seconds = arguments[i + 1].toDouble(&ok);
            if (!ok || seconds <= 0.0)
                return std::nullopt;
            options.selfTest = true;
            options.duration = std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
            options.output = arguments[i + 2];
            i += 2;
        } else if (options.file.isEmpty()) {
            // The player takes only absolute paths (and colours); a relative one is meant from here.
            const QString& file = arguments[i];
            options.file = mikserstudio::core::isAllowedMediaPath(file) ? file : QFileInfo(file).absoluteFilePath();
        } else {
            return std::nullopt;
        }
    }
    if (options.file.isEmpty())
        return std::nullopt;
    return options;
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"mikser-studio-monitor-demo"_s);
    QTextStream err(stderr);

    const auto options = parse(QApplication::arguments());
    if (!options) {
        err << "Usage: mikser-studio-monitor-demo <file> [--self-test <seconds> <out.png>]\n";
        return kUsageError;
    }

    const auto runtime = mikserstudio::engine::MltRuntime::initialize();
    if (!runtime) {
        err << runtime.error() << '\n';
        return kUsageError;
    }

    mikserstudio::engine::ClipPlayer player(**runtime, mikserstudio::engine::AudioOutput::Device);
    mikserstudio::demo::DemoWindow window(player, QApplication::translate("main", "Source Monitor"));
    player.setFrameSink(window.monitorPanel()->frameSink());
    QObject::connect(&player, &mikserstudio::engine::MltPlayer::errorOccurred, &app,
                     [&err](const QString& message) { err << message << Qt::endl; });

    if (auto opened = player.openFile(options->file); !opened) {
        err << opened.error() << '\n';
        return 1;
    }
    window.show();

    std::unique_ptr<mikserstudio::demo::SelfTest> selfTest;
    if (options->selfTest) {
        selfTest = std::make_unique<mikserstudio::demo::SelfTest>(player, window, options->duration, options->output);
        QTimer::singleShot(0, selfTest.get(), &mikserstudio::demo::SelfTest::start);
    }
    return QApplication::exec();
}
