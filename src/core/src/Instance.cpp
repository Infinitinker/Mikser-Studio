// Which Mikser Studio runs, from MIKSERSTUDIO_INSTANCE and the build: its name and app id.
#include "mikserstudio/core/Instance.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

std::expected<Instance, QString> instanceNamed(const QString& value, UnsetInstance unset)
{
    const QString name = u"Mikser Studio"_s;
    const QString appId = QString::fromLatin1(kAppId);
    // Each instance appends its suffix to the installed app's name and id.
    const auto instance = [&](const QString& nameSuffix, const QString& idSuffix) {
        return Instance{.name = name + nameSuffix, .appId = appId + idSuffix};
    };
    if (value.isEmpty() && unset == UnsetInstance::Installed)
        return instance({}, {});
    if (value.isEmpty() || value == u"dev")
        return instance(u" Dev"_s, u".dev"_s);
    if (value == u"e2e")
        return instance(u" E2E"_s, u".e2e"_s);
    return std::unexpected(
        u"MIKSERSTUDIO_INSTANCE is “%1”, which Mikser Studio doesn’t know. Set it to dev or e2e, or leave it unset."_s
            .arg(value));
}

UnsetInstance unsetInstance()
{
#ifdef MIKSERSTUDIO_INSTALLED_APP
    return UnsetInstance::Installed;
#else
    return UnsetInstance::Dev;
#endif
}

std::expected<Instance, QString> currentInstance()
{
    return instanceNamed(qEnvironmentVariable("MIKSERSTUDIO_INSTANCE"), unsetInstance());
}

void applyInstance(const Instance& instance)
{
    // No organization name, so QStandardPaths gives "<AppData>/Mikser Studio" rather than a nested
    // folder. The domain is the reversed app id, which macOS uses for the bundle's preferences.
    QCoreApplication::setApplicationName(instance.name);
    QCoreApplication::setOrganizationDomain(u"infinitinker.github.io"_s);
}

}  // namespace mikserstudio::core
