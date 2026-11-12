// Entry point of the GPU test: unlike the other engine tests, it runs on the machine's own Qt platform,
// so the monitor draws with the GPU in a real window.
#include <catch2/catch_session.hpp>

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
