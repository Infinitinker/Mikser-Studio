// Which Mikser Studio runs, from MIKSERSTUDIO_INSTANCE: its name and app id.
#include "Instance.h"

#include "AppId.h"

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

std::expected<Instance, QString> instanceNamed(const QString& value)
{
    const QString name = u"Mikser Studio"_s;
    const QString appId = QString::fromLatin1(kAppId);
    if (value.isEmpty())
        return Instance{.name = name, .appId = appId};
    if (value == u"dev")
        return Instance{.name = name + u" Dev"_s, .appId = appId + u".dev"_s};
    if (value == u"e2e")
        return Instance{.name = name + u" E2E"_s, .appId = appId + u".e2e"_s};
    return std::unexpected(
        u"MIKSERSTUDIO_INSTANCE is \"%1\", which Mikser Studio doesn't know. Set it to dev or e2e, or leave it unset."_s
            .arg(value));
}

std::expected<Instance, QString> currentInstance()
{
    return instanceNamed(qEnvironmentVariable("MIKSERSTUDIO_INSTANCE"));
}

}  // namespace mikserstudio::app
