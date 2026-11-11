#include "SelfTest.h"

#include "DemoWindow.h"
#include "mikserstudio/engine/ClipPlayer.h"
#include "mikserstudio/engine/MonitorPanel.h"
#include "mikserstudio/engine/MonitorWindow.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QImage>
#include <QSet>
#include <QTextStream>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace mikserstudio::demo {

namespace {

constexpr int kSampleGrid = 24;
constexpr qsizetype kMinDistinctColors = 16;

}  // namespace

SelfTest::SelfTest(engine::ClipPlayer& player, DemoWindow& window, std::chrono::milliseconds duration,
                   QString outputPath)
    : m_player(player)
    , m_window(window)
    , m_duration(duration)
    , m_outputPath(std::move(outputPath))
{
}

void SelfTest::start()
{
    m_player.play();
    QTimer::singleShot(m_duration / 3, this, &SelfTest::floatDock);
    QTimer::singleShot(m_duration * 2 / 3, this, &SelfTest::redock);
    QTimer::singleShot(m_duration, this, &SelfTest::finish);
}

void SelfTest::floatDock()
{
    m_presentedBeforeFloat = m_window.monitorPanel()->monitorWindow()->framesPresented();
    QDockWidget* dock = m_window.monitorDock();
    dock->setFloating(true);
    dock->resize(860, 520);
    dock->move(m_window.geometry().topLeft() + QPoint(120, 120));
}

void SelfTest::redock()
{
    m_presentedAtRedock = m_window.monitorPanel()->monitorWindow()->framesPresented();
    m_window.monitorDock()->setFloating(false);
}

QString SelfTest::checkImage(const QImage& image) const
{
    if (image.isNull() || image.width() < kSampleGrid || image.height() < kSampleGrid)
        return u"the read-back image is empty"_s;
    QSet<QRgb> colors;
    for (int y = 0; y < kSampleGrid; ++y) {
        for (int x = 0; x < kSampleGrid; ++x)
            colors.insert(image.pixel(x * image.width() / kSampleGrid, y * image.height() / kSampleGrid));
    }
    if (colors.size() < kMinDistinctColors)
        return u"the read-back image is nearly uniform (%1 distinct colours in %2 samples), so no video frame is shown"_s
            .arg(colors.size())
            .arg(kSampleGrid * kSampleGrid);
    return {};
}

void SelfTest::finish()
{
    engine::MonitorWindow* monitor = m_window.monitorPanel()->monitorWindow();
    const auto image = monitor->grabFramebuffer();
    const quint64 presented = monitor->framesPresented();
    const quint64 whileFloating = m_presentedAtRedock - m_presentedBeforeFloat;
    const quint64 afterRedock = presented - m_presentedAtRedock;

    QTextStream out(stdout);
    out << "backend: " << monitor->backendName() << '\n'
        << "audio output: " << m_player.audioOutputName() << '\n'
        << "frames received: " << monitor->framesReceived() << '\n'
        << "frames presented: " << presented << " (while floating: " << whileFloating
        << ", after re-docking: " << afterRedock << ")\n"
        << "frames dropped: " << monitor->framesDropped() << '\n'
        << "position: " << m_player.position() << " / " << m_player.duration() << " frames\n";

    QStringList failures;
    if (presented == 0)
        failures.append(u"no frames were presented"_s);
    if (whileFloating == 0)
        failures.append(u"no frames were presented while the dock was floating"_s);
    if (afterRedock == 0)
        failures.append(u"no frames were presented after re-docking"_s);
    if (!image) {
        failures.append(u"read-back failed: %1"_s.arg(image.error()));
    } else {
        out << "read-back: " << image->width() << 'x' << image->height() << '\n';
        if (const QString problem = checkImage(*image); !problem.isEmpty())
            failures.append(problem);
        if (!image->save(m_outputPath))
            failures.append(u"could not write %1"_s.arg(QDir::toNativeSeparators(m_outputPath)));
        else
            out << "saved: " << QDir::toNativeSeparators(m_outputPath) << '\n';
    }

    for (const QString& failure : failures)
        out << "FAILED: " << failure << '\n';
    out << (failures.isEmpty() ? "self-test passed" : "self-test failed") << '\n';
    out.flush();
    QApplication::exit(failures.isEmpty() ? 0 : 1);
}

}  // namespace mikserstudio::demo
