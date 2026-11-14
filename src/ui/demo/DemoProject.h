// Fills the UI demo's project with stand-in media items and a small edit, through the project commands.
#pragma once

#include <QString>
#include <QStringList>

#include <expected>

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::ui::demo {

// Creates empty stand-in media files in `mediaDirectory` (the FakeMediaProbe only looks at their
// names) and builds a small edit on V1, V2, an added V3, A1 and A2 (muted) through the project and
// timeline commands.
// Returns the ids of the clips it placed, or the first error.
std::expected<QStringList, QString> populateDemoProject(core::CommandBus& bus, const core::ProjectModel& project,
                                                        const QString& mediaDirectory);

}  // namespace mikserstudio::ui::demo
