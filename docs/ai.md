# AI in Mikser Studio

Mikser Studio runs no AI models itself and ships none. It connects to the AI tools the user runs,
local and offline first, through their own APIs: one backend per API, such as `ollama` for Ollama and
`comfyui` for ComfyUI. An online service is used only when the user sets it up. Mikser Studio never
downloads models or installs extensions or nodes unless the user asks.

The app runs AI jobs itself, in C++, in `src/services` (`MikserStudio::services`, which depends on the
core, Qt Network and Qt WebSockets). `services::AiEngine` holds the backends and the jobs, and
`services::registerAiCommands` offers them as ordinary commands (`ai.*`, category "AI") on the
`CommandBus`. Menus, the AI panel, the command palette, macros, plugins, the `/v1` API and the MCP
bridge all use them the same way. No other process is started.

```
 AI panel / API / MCP bridge        Mikser Studio (C++, main thread)                   AI tools the user runs
 ───────────────────────────  ai.*  ───────────────────────────────────  HTTP, WebSocket  ──────────────────────
  ai.run  ──────────────────────►  AiEngine: one queue per backend
  ai.jobs (poll) ◄───────────────   ├─ OllamaBackend   (text.generate)  ─────────────────►  Ollama
  ai.cancel_job  ────────────────►  ├─ ComfyUiBackend  (image.generate) ─────────────────►  ComfyUI
                                    └─ on success: project.import_media with the provenance, one undo step
```

## The engine and its jobs

`ai.run` checks the parameters against the operation's JSON Schema, applies its defaults and queues a
job at once. It answers with the job id (`aijob-<uuid>`); `ai.jobs` then shows how the job goes. The
AI panel polls `ai.jobs` to list the jobs.

- **One at a time per backend.** Jobs of one backend run in order, one at a time, since AI tools on
  one GPU don't share it well. Jobs of different backends run side by side.
- **Never blocking.** The engine runs on the main thread, but every request to a tool is asynchronous
  and has a timeout, so a slow or hung tool never blocks the interface.
- **Outputs.** A job writes its files to `<cache>/ai/<job id>/`, in the instance's cache folder.
  A job that its tool fails, or that is canceled while it runs, leaves no folder; a text job, which
  makes no files, leaves none either.
- **Into the project.** When a job succeeds, its image, video and audio outputs are imported with
  `project.import_media` and the job's provenance, as one undo step ("Add AI result"). Each media item
  is named after the start of the prompt, numbered when there are several. The job's `placement` is then
  `{mediaIds}`. Text stays in the job's `result`. If an import fails, the job fails and says which
  file; the items imported before it stay, and the same undo removes them.
- **A replaced project.** A job whose project was replaced (`project.new`, `project.open`) before it
  finished fails without touching the new project. Its files stay in the cache.
- **Canceling** a queued job takes it off the queue. Canceling a running job stops its requests and
  deletes its folder; what the tool does then is up to the backend (see below). Closing the app
  cancels the running jobs the same way, and waits up to 3 s for the requests that take their work
  off the tools' queues.
- **History.** `ai.jobs` lists the jobs of this run of the app, oldest first, with the last 200
  finished ones.

## Commands

| Command | Params | Result |
|---|---|---|
| `ai.run` | `{op, params = {}, backend?}` | `{jobId}` |
| `ai.jobs` | — | `{jobs: [job, …]}`, read-only |
| `ai.cancel_job` | `{jobId}` | `{jobId, state}` |
| `ai.backends` | — | `{backends: [{id, title, ops: [{id, title, description, paramsSchema}], available, reason}]}` |
| `ai.status` | — | `{activeJobs, ollamaUrl, comfyuiUrl}`, read-only |
| `ai.settings` | — | `{ollamaUrl, comfyuiUrl}`, read-only |
| `ai.set_settings` | `{ollamaUrl?, comfyuiUrl?}` | `{ollamaUrl, comfyuiUrl}` |

`ai.run` runs `op` on the backend named in `backend`, or on the first backend that offers it, an
available one first. It answers `invalid_params` for an unknown operation or backend, and for params
the operation's schema refuses ("Invalid parameters for “image.generate”: …"). It does not refuse a
backend that is not available: the job fails with the tool's reason. `ai.cancel_job` answers
`not_available` for a job that has already finished.

## A job

```json
{
  "id": "aijob-5c1e…", "op": "image.generate", "backend": "comfyui",
  "title": "Generate image: A lighthouse at dusk",
  "params": {"prompt": "A lighthouse at dusk", "negative_prompt": "", "width": 1024, "height": 1024, "steps": 25},
  "state": "succeeded",              // queued | running | succeeded | failed | canceled
  "progress": 1.0,                   // 0 to 1
  "message": "Done",                 // a short status for the interface
  "error": "",                       // an English message for the user when the job failed
  "createdAt": "2026-11-13T09:12:04Z", "startedAt": "…", "finishedAt": "…",   // "" until then
  "result": {
    "seed": 81723645012,             // the operation's own fields: "text" and "model" for text.generate
    "outputs": [{"path": "C:/…/cache/ai/aijob-5c1e…/image-1.png", "kind": "image"}],
    "provenance": {
      "jobId": "aijob-5c1e…", "backend": "comfyui", "op": "image.generate",
      "params": {"prompt": "A lighthouse at dusk", "negative_prompt": "", "width": 1024, "height": 1024,
                 "steps": 25, "seed": 81723645012},
      "createdAt": "2026-11-13T09:13:30Z",
      "app": {"name": "Mikser Studio", "version": "0.1.0"},
      "tools": {"comfyui": "0.4.2"},
      "model": {"name": "qwen_image_2.1_int8_convrot.safetensors",
                "textEncoder": "qwen3vl_8b_int8_convrot.safetensors", "vae": "qwen_image_2.1_vae_bf16.safetensors"},
      "method": {"workflow": "Qwen Image 2.1 text to image", "sampler": "euler", "scheduler": "simple",
                 "cfg": 1, "steps": 25, "seed": 81723645012, "width": 1024, "height": 1024, "promptId": "…"}
    }
  },
  "placement": {"mediaIds": ["media-…"]}
}
```

The title is the operation's, with the start of the prompt after it. The `result` holds the
operation's own fields, the `outputs` (`kind` is `image`, `video` or `audio`) and the `provenance`.
The backend gives the provenance's `tools`, `model` and `method`; the engine adds the job, backend,
operation, params, time and app version. A seed the backend picked goes into the provenance's
`params`, so the result can be made again.

## Backends

| Backend | Operation | What it does |
|---|---|---|
| `ollama` | `text.generate {prompt, model?}` | Streams `/api/generate` from the user's Ollama. Without `model` it uses the first installed model whose capabilities include `completion` (vision-only or embedding models are skipped). Result: `{text, model}`; reasoning that a thinking model writes before `</think>` is dropped from `text`. Provenance: Ollama's version and the model's name, digest, family, size and quantization. Canceling closes the connection, which stops Ollama's answer. Unavailable when Ollama is not running or has no models. |
| `comfyui` | `image.generate {prompt, negative_prompt = "", width = 1024, height = 1024, steps = 25, seed?}` | Runs ComfyUI's Qwen Image 2.1 text-to-image workflow, without its prompt enhancer, in the user's ComfyUI: the diffusion model `qwen_image_2.1_int8_convrot.safetensors` through `QwenImage21Cache`, the text encoder `qwen3vl_8b_int8_convrot.safetensors` with `TextEncodeQwenImage21`, KSampler (euler, simple, cfg 1), the VAE `qwen_image_2.1_vae_bf16.safetensors`, and `SaveImage` into `mikserstudio/<job id>` in ComfyUI's output folder. The image is then copied into the job's folder. Sizes are rounded down to a multiple of 16. Without `seed` it picks one and returns it as `{seed}`. Progress comes from ComfyUI's socket. Provenance: ComfyUI's version, the three models, and the sampler, steps, seed, size and ComfyUI's prompt id. Unavailable when ComfyUI is not running or lacks one of the models or nodes; the reason names them. |

The `comfyui` backend uses only ComfyUI's local nodes, never its API nodes (whose names end in
`Api`), which would send the work to an online service. It never calls `/interrupt` or `/free`, which
would stop or unload whatever ComfyUI runs for someone else. Canceling deletes only the job's own
queued prompt, with `POST /queue {"delete": [<prompt id>]}`; a prompt that already runs is left to
finish, and its image stays in ComfyUI's output folder.

## Settings

`ai-settings.json` in the instance's config folder holds the tools' addresses:

```json
{"ollamaUrl": "http://127.0.0.1:11434", "comfyuiUrl": "http://127.0.0.1:8188"}
```

These are also the defaults. `ai.set_settings` changes one address or both and saves the file. An
address must be an `http` or `https` URL with a host; the error names the setting. A new address
takes effect at once: new jobs and checks use it, and running jobs go on with the old one. A file that
is missing or cannot be read gives the defaults, and the log says why.

## Availability and timeouts

`ai.backends` answers at once with what the engine knows, and checks the backends again in the
background, unless a check is under way or the last one is less than 5 s old. A backend not checked
yet is listed as unavailable, with the reason "Mikser Studio is checking whether ComfyUI is running.
Ask again in a moment." A new address forgets what was known about its backend. A reason always says
what to do, such as starting the tool or downloading a model.

Every request gives up when no data comes for its timeout:

| Tool | Requests | Timeout |
|---|---|---|
| Ollama | `/api/version`, `/api/tags` | 3 s |
| Ollama | `/api/show` (reads the model file) | 15 s |
| Ollama | `/api/generate` | 600 s of silence: loading a large model can take minutes |
| ComfyUI | `/system_stats`, `/models`, `/object_info`, `/history`, `/queue` | 5 s |
| ComfyUI | `/prompt` (ComfyUI checks the whole workflow) | 30 s |
| ComfyUI | `/view`, per image | 120 s |
| ComfyUI | opening the socket (`/ws`) | 10 s |
| ComfyUI | a queued or running prompt | 10 min with no socket message: the prompt is taken off the queue and the job fails |

## Adding a backend

A backend is one class in `src/services/src/` that implements `AiBackend` (`AiBackend.h`, private to
the module):

- `id()` and `title()`, such as `comfyui` and `ComfyUI`.
- `operations()`: each with an id, a title, a description and the JSON Schema of its params, with
  defaults. `core::JsonSchema` checks them. Several backends can offer one operation id.
- `setBaseUrl()`: the tool's address. New tasks and checks use it; running tasks keep theirs.
- `checkAvailability()`: a few short requests, and a reason the user can act on when the tool can't
  run.
- `start()`: returns an `AiTask` for one job. The task writes only into `outputDirectory`, reports
  progress with `report`, and calls `finish` once, with an `AiOutcome` (outputs, its part of the
  provenance, and the operation's own fields) or an English message for the user. `cancel()` aborts
  its requests and never stops work in the tool that is not its own.

Requests go through `sendRequest` and `sendJsonRequest` (`HttpRequests.h`), which take a timeout, and
never through a proxy. Then:

1. Register the backend in `AiEngine`'s constructor with `addBackend`.
2. If it has an address, add it to `AiSettings` (its JSON key and check in `AiSettings.cpp`), pass it
   to the backend in `AiEngine::setSettings`, and add it to the schema of `ai.set_settings`.
   `ai.settings` and `ai.status` list it from there.
3. Add a fake of the tool to `tests/services/FakeAiTools.*` and test the backend against it.

## Tests

`tests/services` (Catch2) runs the engine, the backends and the commands against fake Ollama and
ComfyUI servers in the test process (`tests/services/FakeAiTools.*`), on ephemeral loopback ports. The
fakes answer the parts of the tools' APIs the backends use and record what they were asked, so the
tests check the requests too, such as that canceling deletes only the job's own prompt.

`tests/e2e/AiRunTests.cpp` starts the built app headless, points it at a fake ComfyUI with
`ai.set_settings`, runs `ai.run image.generate` through the local API, and checks that the image comes
into the project with its provenance. So `ctest --preset e2e` covers the whole path without a real AI
tool.
