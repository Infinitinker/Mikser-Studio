// Entry point of the MCP bridge's tests: Catch2's session in a QCoreApplication, whose event loop the
// bridge and the fake Mikser Studio run on.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"MikserStudioMcpTests"_s);
    // Keeps the default discovery location away from the real user profile folders.
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}
