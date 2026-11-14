// Entry point of the plugins' specs: they run in one QCoreApplication, which the fake AI tools need.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"MikserStudioPluginSpecs"_s);
    return Catch::Session().run(argc, argv);
}
