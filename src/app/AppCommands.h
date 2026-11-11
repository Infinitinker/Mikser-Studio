#pragma once

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::engine {
class ClipPlayer;
}  // namespace mikserstudio::engine

namespace mikserstudio::app {

// Commands that tie modules together and so belong to neither of them:
//
//   project.new          {}                -> {}           clears the undo history
//   project.save         {path}            -> {path}       JSON project file (.mikser)
//   project.open         {path}            -> {path}       clears the undo history
//
// project.new and project.open replace the project, so they carry a CommandSpec::confirmation
// that interactive UIs ask before running them.
//   source.open_media    {mediaId}         -> {mediaId}    shows a media item in the source monitor
//
// `source` is null when there is no playback engine; source.open_media is then not registered.
void registerAppCommands(core::CommandBus& bus, core::ProjectModel& project, engine::ClipPlayer* source);

}  // namespace mikserstudio::app
