// The monitor on the machine's GPU: plays the sample video in a docked monitor, floats and re-docks it,
// and checks the picture it drew. Labeled gpu: the default test presets leave it out, the gpu preset
// runs it (docs/development.md, "GPU test").
#include "EngineTestSupport.h"

#include "mikserstudio/engine/ClipPlayer.h"
#include "mikserstudio/engine/MonitorPanel.h"
#include "mikserstudio/engine/MonitorWindow.h"

#include <QDockWidget>
#include <QImage>
#include <QMainWindow>
#include <QSet>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::engine::AudioOutput;
using mikserstudio::engine::ClipPlayer;
using mikserstudio::engine::MonitorPanel;
using mikserstudio::engine::MonitorWindow;
using mikserstudio::tests::waitUntil;

namespace {

constexpr int kSampleGrid = 24;
constexpr qsizetype kMinDistinctColors = 16;

// Distinct colors on a grid of samples: a video frame has many, an empty or cleared monitor few.
qsizetype distinctColors(const QImage& image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < kSampleGrid; ++y) {
        for (int x = 0; x < kSampleGrid; ++x)
            colors.insert(image.pixel(x * image.width() / kSampleGrid, y * image.height() / kSampleGrid));
    }
    return colors.size();
}

}  // namespace

TEST_CASE("On the GPU, a docked monitor shows the video after being floated and docked again", "[gpu]")
{
    const auto sample = mikserstudio::tests::sampleVideo();
    if (!sample)
        SKIP("The sample video " MIKSERSTUDIO_SAMPLE_VIDEO " is not available");

    ClipPlayer player(mikserstudio::tests::mltRuntime(), AudioOutput::Silent);
    QMainWindow window;
    auto* dock = new QDockWidget(u"Source monitor"_s, &window);
    auto* panel = new MonitorPanel(player, dock);
    dock->setWidget(panel);
    window.addDockWidget(Qt::RightDockWidgetArea, dock);
    window.resize(1280, 760);
    player.setFrameSink(panel->frameSink());
    REQUIRE(player.openFile(*sample).has_value());
    window.show();

    MonitorWindow* monitor = panel->monitorWindow();
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    INFO("Backend: " << monitor->backendName().toStdString());
    CHECK(monitor->backendName() != u"Null"_s);

    // Each step must show new frames while the video plays.
    const auto keepsPresenting = [&] {
        const quint64 before = monitor->framesPresented();
        return waitUntil([&] { return monitor->framesPresented() > before + 10; }, 10s);
    };

    player.play();
    REQUIRE(keepsPresenting());

    dock->setFloating(true);
    dock->resize(860, 520);
    dock->move(window.geometry().topLeft() + QPoint(120, 120));
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    CHECK(keepsPresenting());

    dock->setFloating(false);
    REQUIRE(waitUntil([&] { return monitor->isExposed(); }));
    CHECK(keepsPresenting());

    const auto image = monitor->grabFramebuffer();
    REQUIRE(image.has_value());
    REQUIRE(image->width() >= kSampleGrid);
    REQUIRE(image->height() >= kSampleGrid);
    CHECK(distinctColors(*image) >= kMinDistinctColors);
}
