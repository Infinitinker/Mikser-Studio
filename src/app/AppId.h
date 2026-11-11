#pragma once

namespace mikserstudio::app {

// The reverse-DNS application id: the macOS bundle id, the Linux .desktop/Flatpak app id and the
// Windows AppUserModelID once the app is packaged. Never change it after a release: settings,
// file associations and pinned shortcuts are tied to it.
inline constexpr auto kAppId = "io.github.infinitinker.mikserstudio";

}  // namespace mikserstudio::app
