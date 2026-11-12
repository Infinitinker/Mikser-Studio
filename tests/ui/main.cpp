// Entry point of the UI tests: they run in a QApplication on the offscreen platform, so no window appears.
#include <catch2/catch_session.hpp>

#include "mikserstudio/ui/Theme.h"

#include <QApplication>

// UI tests need a QApplication (widgets, docking, QML). They always run on the offscreen platform
// so no window appears.
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    mikserstudio::ui::prepareApplication();
    QApplication app(argc, argv);
    mikserstudio::ui::applyTheme(app);
    return Catch::Session().run(argc, argv);
}
