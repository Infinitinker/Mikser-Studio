// Which Mikser Studio runs: the installed app, Mikser Studio Dev (a build from source) or Mikser Studio
// E2E (the end-to-end tests), as MIKSERSTUDIO_INSTANCE says. Each has its own name and app id, so its
// own per-user folders (UserFolders.h): running from source or testing never touches the files of an
// installed Mikser Studio.
#pragma once

#include <QString>

#include <expected>

namespace mikserstudio::core {

// The reverse-DNS application id: the macOS bundle id, the Linux .desktop/Flatpak app id and the
// Windows AppUserModelID once the app is packaged. Never change it after a release: settings,
// file associations and pinned shortcuts are tied to it.
inline constexpr auto kAppId = "io.github.infinitinker.mikserstudio";

struct Instance {
    QString name;  // "Mikser Studio", "Mikser Studio Dev" or "Mikser Studio E2E": the per-user folders' name.
    QString appId;  // kAppId, with ".dev" or ".e2e" after it for the other two.
};

// What an unset MIKSERSTUDIO_INSTANCE means: the installed app in a release build, which the
// MIKSERSTUDIO_INSTALLED_APP CMake option makes, and Mikser Studio Dev in every other build.
enum class UnsetInstance { Installed, Dev };

// The instance a value of MIKSERSTUDIO_INSTANCE names: "dev", "e2e", or empty for `unset`. Any other
// value is an error, which says what to set instead.
[[nodiscard]] std::expected<Instance, QString> instanceNamed(const QString& value, UnsetInstance unset);

// What an unset MIKSERSTUDIO_INSTANCE means in this build.
[[nodiscard]] UnsetInstance unsetInstance();

// The instance MIKSERSTUDIO_INSTANCE names in this build.
[[nodiscard]] std::expected<Instance, QString> currentInstance();

// Names the application after the instance, so that QStandardPaths, and UserFolders with it, give the
// instance's own folders. Call it once the application object exists.
void applyInstance(const Instance& instance);

}  // namespace mikserstudio::core
