#pragma once

#include "mikserstudio/services/SidecarManager.h"

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <expected>

namespace mikserstudio::services {

// The environment variable that carries the session token to the sidecar.
inline constexpr char16_t kTokenVariable[] = u"MIKSERSTUDIO_AI_TOKEN";

struct SidecarLaunch {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
    // The absolute cache folder. Never the app's own working directory: that may be any folder
    // (e.g. Downloads), and Windows looks for programs in the working directory first.
    QString workingDirectory;
};

// Builds the command line and environment for one launch, or a user-facing error such as
// "The AI service could not start: its Python environment ... was not found. ...".
[[nodiscard]] std::expected<SidecarLaunch, QString> resolveSidecarLaunch(const SidecarOptions& options,
                                                                         const QByteArray& token);

// 32 random bytes from the OS CSPRNG, base64url without padding (43 characters).
[[nodiscard]] QByteArray newSidecarToken();

// The port from a "MIKSERSTUDIO_AI_READY port=<port>" line, or -1 when the line is something else.
[[nodiscard]] int parseReadyLine(QByteArrayView line);

}  // namespace mikserstudio::services
