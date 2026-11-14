// The job kind that runs a workflow in ComfyUI: comfyui.run_prompt.
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

class QNetworkAccessManager;

namespace mikserstudio::core {
class JobManager;
}

namespace mikserstudio::services {

struct ComfyUiJobOptions {
    // The port of the app's own API, which the jobs never reach; nullopt while it isn't serving.
    std::function<std::optional<quint16>()> apiPort;
    // Where each job saves the files ComfyUI made, in a folder named after the job.
    QString outputDirectory;
};

// Registers the kind in `jobs` and runs its jobs; unregisters it when it goes. It needs the "network"
// permission and connects only to the addresses the owner's network entries allow
// (core::NetworkEntry), never to the app's own API.
//
//   comfyui.run_prompt {url, workflow, outputNode, stages?, title?}
//     -> {promptId, outputs: [{path, kind}], provenance: {promptId, comfyui}}
//        or {refused: {status, body, json?}} when ComfyUI doesn't take the workflow.
//
// It opens ComfyUI's socket at `url`, queues `workflow` (ComfyUI's API format), follows the prompt's
// events, with the progress of its steps and, as the message, `stages` (node id -> text) of the node
// that runs, then copies the files that `outputNode` saved into the job's folder. `kind` is "image",
// "video" or "audio", from the file's suffix. The provenance is what the job saw itself: the prompt's
// id and ComfyUI's address. Canceling takes only the job's own prompt off ComfyUI's queue: it never
// interrupts or frees ComfyUI.
class ComfyUiJobs final : public QObject {
    Q_OBJECT

public:
    ComfyUiJobs(core::JobManager& jobs, ComfyUiJobOptions options, QObject* parent = nullptr);
    ~ComfyUiJobs() override;
    ComfyUiJobs(const ComfyUiJobs&) = delete;
    ComfyUiJobs& operator=(const ComfyUiJobs&) = delete;

    class Run;  // One job's prompt in ComfyUI.

private:
    void forget(const QString& id);  // Deletes the run of the job, later.

    core::JobManager& m_jobs;
    ComfyUiJobOptions m_options;
    QNetworkAccessManager* m_network;
    std::map<QString, std::unique_ptr<Run>> m_runs;  // The running jobs.
};

}  // namespace mikserstudio::services
