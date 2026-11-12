// The ai.* commands (status, backends, jobs, cancel, upscale a clip, generate text) and the job tracking behind
// them.
#pragma once

namespace mikserstudio::core {
class CommandBus;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::services {

class AiClient;
class SidecarManager;

// Registers the AI commands (category "AI"). Jobs are tracked here, in C++: each gets a local id
// ("aijob-<uuid>") at once, is sent to the sidecar when it is running (starting it on demand)
// and is polled while active. Results stay in ai.jobs.
//
//   ai.status        {}                     -> {state, error, baseUrl, restarts, activeJobs}   (readOnly)
//   ai.backends      {}                     -> {backends: [{id, title, ops, available, reason}]}
//                                              NotAvailable while the service is starting/failed.
//   ai.jobs          {}                     -> {jobs: [{id, remoteId, op, title, state, progress,
//                                               message, error, createdAt, result, placement}]}
//                                              (readOnly)
//   ai.cancel_job    {jobId}                -> {jobId, state}
//   ai.upscale_clip  {clipId, scale = 2}    -> {jobId}
//   ai.generate_text {prompt, model?}       -> {jobId}
//
// ai.upscale_clip runs "video.upscale" on the clip's media. When it succeeds, one undo step
// ("Place AI result") imports the output with "project.import_media" and the job provenance,
// adds a video track on top with "timeline.add_track" when the clip is on the top video track,
// and places the result with "timeline.add_clip" at the clip's position on the video track above
// the clip's track. The command fails at once when the clip is not a video clip. A job whose
// project was replaced (ProjectModel::projectReset) before it finished fails without changing
// the new project.
//
// The commands and the job tracker belong to `client` (they go away with it, and unregister
// themselves from the bus). `bus`, `project` and `sidecar` may be destroyed first. Call once.
void registerAiCommands(core::CommandBus& bus, core::ProjectModel& project, SidecarManager& sidecar, AiClient& client);

}  // namespace mikserstudio::services
