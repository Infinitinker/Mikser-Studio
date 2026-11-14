// The project and timeline commands (importing and removing media, adding, moving and trimming clips, tracks)
// and how they register on the command bus.
#pragma once

namespace mikserstudio::core {

class CommandBus;
class MediaProbe;
class ProjectModel;

// Registers the project and timeline commands. All of them are undoable except reads.
//
//   project.import_media      {path, name?, provenance?}       -> {mediaId}
//   project.remove_media      {mediaId}
//   project.list              {}                               -> project JSON (readOnly)
//   timeline.add_clip         {mediaId, trackId, position, in?, out?} -> {clipId}
//   timeline.remove_clip      {clipId}
//   timeline.move_clip        {clipId, trackId, position}
//   timeline.trim_clip        {clipId, in, out}
//   timeline.add_track        {kind: "video"|"audio"}          -> {trackId}
//   timeline.set_track_flags  {trackId, muted?, hidden?}
//
// Mutating commands fail with CommandErrorCode::Failed and the model's message when a rule is
// broken (see ProjectModel). Frame params are integers in 0..kMaxFrame. import_media probes the
// file with `probe` and stores `provenance` (of generated media) on the item; still images get a
// duration of 5 seconds. add_clip defaults `in` to 0 and `out` to the end of the media.
// set_track_flags keeps omitted flags and records no undo step when nothing changes.
// Undo restores the exact previous state (ids, media bin order, clip placement).
//
// The project's changes are published on the bus's events, with the id of what changed:
// "media.added" and "media.removed" {mediaId}, "clip.added", "clip.removed" and "clip.changed"
// {clipId}, "track.added", "track.removed" and "track.changed" {trackId}, "project.reset" {} and
// "project.file_changed" {path}.
//
// `probe`, `project` and `bus` must outlive the registered commands.
void registerProjectCommands(CommandBus& bus, ProjectModel& project, MediaProbe& probe);

}  // namespace mikserstudio::core
