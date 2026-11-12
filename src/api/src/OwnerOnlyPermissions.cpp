#include "OwnerOnlyPermissions.h"

#include <QDir>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>

#include <aclapi.h>

#include <string>
#include <vector>
#endif

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

#ifdef Q_OS_WIN

namespace {

QString windowsError(DWORD code)
{
    return u"Windows error %1"_s.arg(code);
}

}  // namespace

std::expected<void, QString> restrictToOwner(const QString& path)
{
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == 0)
        return std::unexpected(windowsError(GetLastError()));

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buffer(size);
    const bool gotUser = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0;
    const DWORD tokenError = GetLastError();
    CloseHandle(token);
    if (!gotUser)
        return std::unexpected(windowsError(tokenError));
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_ALL;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(user->User.Sid);

    PACL acl = nullptr;
    if (const DWORD error = SetEntriesInAclW(1, &access, nullptr, &acl); error != ERROR_SUCCESS)
        return std::unexpected(windowsError(error));

    std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const DWORD error = SetNamedSecurityInfoW(nativePath.data(), SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                              nullptr, acl, nullptr);
    LocalFree(acl);
    if (error != ERROR_SUCCESS)
        return std::unexpected(windowsError(error));
    return {};
}

#else

std::expected<void, QString> restrictToOwner(const QString& path)
{
    QFile file(path);
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return std::unexpected(file.errorString());
    return {};
}

#endif

}  // namespace mikserstudio::api
