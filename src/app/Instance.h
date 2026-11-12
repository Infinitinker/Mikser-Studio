// Which Mikser Studio runs: the installed app, the app run from source (start.bat) or the app under
// the end-to-end tests, as MIKSERSTUDIO_INSTANCE says. Each has its own name and app id, so its own
// per-user folders, API discovery file and settings: running from source or testing never touches
// the files of an installed Mikser Studio.
#pragma once

#include <QString>

#include <expected>

namespace mikserstudio::app {

struct Instance {
    QString name;  // "Mikser Studio", "Mikser Studio Dev" or "Mikser Studio E2E": the per-user folders' name.
    QString appId;  // kAppId, with ".dev" or ".e2e" after it for the other two.
};

// The instance a value of MIKSERSTUDIO_INSTANCE names: "dev", "e2e", or empty for the installed app.
// Any other value is an error, which says what to set instead.
[[nodiscard]] std::expected<Instance, QString> instanceNamed(const QString& value);

// The instance MIKSERSTUDIO_INSTANCE names.
[[nodiscard]] std::expected<Instance, QString> currentInstance();

}  // namespace mikserstudio::app
