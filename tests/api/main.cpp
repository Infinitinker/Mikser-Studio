#include <catch2/catch_session.hpp>

#include <QCoreApplication>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"MikserStudioApiTests"_s);
    // Keeps the default discovery location away from the real user profile folders.
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}
