// The E2E tests' entry point: Catch2's session in a QCoreApplication, named like the E2E instance so
// that QStandardPaths gives the tests the folders the app under test uses.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"Mikser Studio E2E"_s);
    return Catch::Session().run(argc, argv);
}
