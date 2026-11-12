// The discovery file: it describes the running local API, only the current user can read it, it is removed
// on stop, a file taken over by another instance is kept, and an unwritable path fails the start.
#include "ApiTestSupport.h"

#include "DiscoveryFile.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>

#include <aclapi.h>

#include <string>
#include <vector>
#endif

using namespace Qt::StringLiterals;
using mikserstudio::tests::ApiFixture;

namespace {

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

#ifdef Q_OS_WIN
// True when the DACL is protected and holds exactly one allow entry, for the current user.
bool onlyCurrentUserHasAccess(const QString& path)
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
        return false;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    const bool gotUser = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
    CloseHandle(token);
    if (!gotUser)
        return false;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

    std::wstring nativePath = path.toStdWString();
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (GetNamedSecurityInfoW(nativePath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &descriptor) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    GetSecurityDescriptorControl(descriptor, &control, &revision);
    bool result = dacl != nullptr && (control & SE_DACL_PROTECTED) != 0 && dacl->AceCount == 1;
    if (result) {
        void* ace = nullptr;
        result = GetAce(dacl, 0, &ace) != 0 && static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                 EqualSid(&static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart, user->User.Sid) != 0;
    }
    LocalFree(descriptor);
    return result;
}
#endif

}  // namespace

TEST_CASE("The discovery file describes the running server and is removed on stop", "[api][discovery]")
{
    ApiFixture fixture;
    const QString path = fixture.server->discoveryFilePath();
    REQUIRE(QFileInfo::exists(path));

    const QJsonObject discovery = readJson(path);
    CHECK(discovery.value(u"pid"_s).toInteger() == QCoreApplication::applicationPid());
    CHECK(discovery.value(u"port"_s).toInt() == fixture.server->port());
    CHECK(discovery.value(u"token"_s).toString() == fixture.server->token());
    CHECK(discovery.value(u"apiVersion"_s).toInt() == 1);
    CHECK(discovery.value(u"baseUrl"_s).toString() == u"http://127.0.0.1:%1"_s.arg(fixture.server->port()));
    CHECK(discovery.value(u"eventsUrl"_s).toString() == u"ws://127.0.0.1:%1/v1/events"_s.arg(fixture.server->port()));
    // Written atomically: no temporary file is left next to it.
    CHECK(QDir(fixture.dir.path()).entryList(QDir::Files | QDir::Hidden) == QStringList{u"api.json"_s});

    // A tool that reads only the discovery file can call the API.
    const auto reply = mikserstudio::tests::sendRequest(
        "GET", QUrl(discovery.value(u"baseUrl"_s).toString() + u"/v1/commands"_s), {},
        "Bearer " + discovery.value(u"token"_s).toString().toUtf8());
    CHECK(reply.status == 200);

    SECTION("stop() removes it")
    {
        fixture.server->stop();
        CHECK_FALSE(QFileInfo::exists(path));
    }
    SECTION("destruction removes it")
    {
        fixture.server.reset();
        CHECK_FALSE(QFileInfo::exists(path));
    }
    SECTION("a file taken over by another instance is kept")
    {
        QJsonObject other = discovery;
        other.insert(u"token"_s, u"another-instance-token"_s);
        REQUIRE(mikserstudio::api::writeDiscoveryFile(path, other).has_value());
        fixture.server->stop();
        CHECK(QFileInfo::exists(path));
    }
}

TEST_CASE("The discovery file is readable only by the current user", "[api][discovery]")
{
    ApiFixture fixture;
    const QString path = fixture.server->discoveryFilePath();
#ifdef Q_OS_WIN
    CHECK(onlyCurrentUserHasAccess(path));
#else
    const QFileDevice::Permissions permissions = QFile::permissions(path);
    CHECK((permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther |
                          QFileDevice::WriteOther)) == QFileDevice::Permissions{});
    CHECK(permissions.testFlags(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
#endif
    // The owner can still replace and remove it.
    REQUIRE(mikserstudio::api::writeDiscoveryFile(path, readJson(path)).has_value());
    fixture.server->stop();
    CHECK_FALSE(QFileInfo::exists(path));
}

TEST_CASE("start() refuses to run without a discovery file to write", "[api][discovery]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::api::ApiServer server(bus, {.port = 0, .token = {}, .discoveryFile = {}});
    const auto started = server.start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().contains(u"no discovery file"_s));
    CHECK_FALSE(server.isRunning());
}

TEST_CASE("start() fails cleanly when the discovery file cannot be written", "[api][discovery]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString blocker = dir.filePath(u"not-a-folder"_s);
    {
        QFile file(blocker);
        REQUIRE(file.open(QIODevice::WriteOnly));
    }
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::api::ApiServer server(bus, {.port = 0, .token = {}, .discoveryFile = blocker + u"/api.json"_s});

    const auto started = server.start();
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().contains(u"discovery file"_s));
    CHECK_FALSE(server.isRunning());
    CHECK(server.port() == 0);
}
