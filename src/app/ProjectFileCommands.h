// The commands that start, open and save project files: project.new, project.open, project.save and
// project.save_as.
#pragma once

#include <QString>

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::app {

// Registers the project file commands:
//
//   project.new      {discard = false}                 -> {}      an empty project, with no file
//   project.open     {path, discard = false}           -> {path}  a .msproj file
//   project.save     {path?, overwrite = false}        -> {path}  Ctrl+S: the project's own file,
//                                                                 or `path` when given
//   project.save_as  {path, overwrite = false}         -> {path}  Ctrl+Shift+S
//
// The project remembers the file it was opened from or last saved to (ProjectModel::filePath), and
// a save or an open marks the undo history clean (UndoStack::setClean), so the history says
// whether there are unsaved changes.
//
// Asking first, as other desktop apps do:
// - Saving to the project's own file never asks. Saving over another existing file fails with
//   NeedsConfirmation unless `overwrite` (the save commands' CommandSpec::confirmationParam) is set.
// - project.new and project.open fail with NeedsConfirmation while there are unsaved changes,
//   unless `discard` (their confirmationParam) is set.
//
// Paths: a path that doesn't end with .msproj (in any case) gets it, so "Cut v1.2" saves as
// "Cut v1.2.msproj". project.open opens the path as given when that file exists, and otherwise the
// path with .msproj added. A folder, or a path that ends with a slash, is refused.
void registerProjectFileCommands(core::CommandBus& bus, core::ProjectModel& project);

// The file a project saved to `path` goes to: `path`, with ".msproj" added unless it already ends
// with it, in any case.
[[nodiscard]] QString projectFilePath(const QString& path);

}  // namespace mikserstudio::app
