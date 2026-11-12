// Tells the person who started Mikser Studio why it stops at start-up, where they can see it.
#pragma once

#include <QString>

namespace mikserstudio::app {

// How Mikser Studio was started: as the desktop app, or headless from a terminal or a script.
enum class StartedAs { Desktop, Headless };

// Logs `message` as critical and makes sure the person who started Mikser Studio sees it. Qt's own
// output reaches the terminal only in a console build; a build without a console (the release on
// Windows) writes it to stderr itself when stderr goes somewhere (a pipe, a file), else to the
// terminal it was started from. Headless with neither, it shows the message in a box; the desktop app
// always shows the box.
void reportFatalError(const QString& message, StartedAs startedAs);

}  // namespace mikserstudio::app
