#include <catch2/catch_session.hpp>

#include <QApplication>

// Engine tests need a QApplication (monitor widgets, queued signals). They always run on the
// offscreen platform so no window appears and the monitor uses the QRhi Null backend.
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
