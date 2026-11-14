// The commands that tie modules together and belong to none of them: the project file commands and
// source.open_media.
#pragma once

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::engine {
class ClipPlayer;
}  // namespace mikserstudio::engine

namespace mikserstudio::app {

// Commands that tie modules together and so belong to none of them: the project file commands
// (ProjectFileCommands.h) and
//
//   source.open_media    {mediaId}         -> {mediaId}    shows a media item in the source monitor
//
// `source` is null when there is no playback engine; source.open_media is then not registered.
void registerAppCommands(core::CommandBus& bus, core::ProjectModel& project, engine::ClipPlayer* source);

}  // namespace mikserstudio::app
