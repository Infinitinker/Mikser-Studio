#include <catch2/catch_session.hpp>

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"MikserStudioServicesTests"_s);
    return Catch::Session().run(argc, argv);
}
