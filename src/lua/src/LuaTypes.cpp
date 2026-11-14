// The types of the ms API: types.lua, compiled in, with the permissions and error codes filled in.
#include "mikserstudio/lua/LuaTypes.h"

#include "Types.h"

#include "mikserstudio/core/Command.h"
#include "mikserstudio/core/CommandBus.h"

#include <QStringList>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

namespace {

// A LuaLS alias of string literals: "a"|"b".
QString literals(const QStringList& values)
{
    QStringList quoted;
    for (const QString& value : values)
        quoted.append(u'"' + value + u'"');
    return quoted.join(u'|');
}

}  // namespace

QString luaTypes()
{
    using enum core::CommandErrorCode;
    QStringList codes;
    for (const core::CommandErrorCode code :
         {InvalidParams, PermissionDenied, NeedsConfirmation, Canceled, NotAvailable, UnknownCommand, Failed})
        codes.append(core::toString(code));
    return QString::fromUtf8(kTypes)
        .replace(u"{permissions}"_s, literals(core::allPermissions()))
        .replace(u"{error_codes}"_s, literals(codes));
}

}  // namespace mikserstudio::lua
