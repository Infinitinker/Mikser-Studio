#include <catch2/catch_session.hpp>

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    // A QCoreApplication, like the headless studio.
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"MikserStudioAppTests"_s);
    return Catch::Session().run(argc, argv);
}
