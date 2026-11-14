// The ai.* commands: run an AI operation, list the backends and jobs, cancel a job, and the AI settings.
#pragma once

namespace mikserstudio::core {
class CommandBus;
class JobManager;
class ProjectModel;
}  // namespace mikserstudio::core

namespace mikserstudio::services {

class AiEngine;

// Registers the AI commands (category "AI") on the engine:
//
//   ai.run          {op, params = {}, backend?} -> {jobId}
//   ai.jobs         {}                          -> {jobs: [{id, op, backend, title, params, state, progress,
//                                                   message, error, createdAt, startedAt, finishedAt, result,
//                                                   placement}]}                                      (readOnly)
//   ai.cancel_job   {jobId}                     -> {jobId, state}
//   ai.backends     {}                          -> {backends: [{id, title, ops, available, reason}]}
//   ai.status       {}                          -> {activeJobs, comfyuiUrl}                          (readOnly)
//   ai.settings     {}                          -> {comfyuiUrl}                                      (readOnly)
//   ai.set_settings {comfyuiUrl?}               -> {comfyuiUrl}
//
// When an ai.run job succeeds, its image, video and audio outputs are imported into the project
// with the job's provenance, as one undo step ("Add AI result"), and the job's placement is
// {mediaIds}; text comes back in the job's result ("text"). A job whose project was replaced
// (ProjectModel::projectReset) before it finished fails without changing the new project.
//
// The commands belong to `engine` (they go away with it, and unregister themselves from the bus).
// `bus` and `project` may be destroyed first. Call once.
void registerAiCommands(core::CommandBus& bus, core::ProjectModel& project, AiEngine& engine);

// Mirrors the engine's jobs in `jobs`, with the same ids: kind "ai.<op>", lane "ai:<backend>", and
// the result {result, placement}. jobs.cancel cancels the engine's job. `jobs` must be destroyed
// before `engine`.
void mirrorAiJobs(AiEngine& engine, core::JobManager& jobs);

}  // namespace mikserstudio::services
