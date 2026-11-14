# AI in Mikser Studio

Mikser Studio runs no AI models itself and ships none. It connects to the AI tools the user runs,
local and offline first, through their own APIs: one plugin or backend per API, such as the `ollama` plugin
for Ollama and the `comfyui` backend for ComfyUI. An online service is used only when the user sets it up. Mikser Studio never
downloads models or installs extensions or nodes unless the user asks.

Each AI tool is moving into a plugin of its own: the plugin holds the policy (which model, what to
send, how to word a failure) in Lua, and the app's C++ job kinds do the network work. Ollama has moved:
the built-in `ollama` plugin ([below](#the-ollama-plugin)) runs on the `http.request` and
`http.stream_ndjson` job kinds ([plugins.md](plugins.md#job-kinds)). ComfyUI still runs in the AI
engine, in C++, in `src/services` (`MikserStudio::services`, which depends on the core, Qt Network and
Qt WebSockets): `services::AiEngine` holds its backend and jobs, and `services::registerAiCommands`
offers them as ordinary commands (`ai.*`, category "AI") on the `CommandBus`, until it becomes the
`comfyui` plugin. Menus, the AI panel, the command palette, macros, plugins, the `/v1` API and the MCP
bridge use the plugins' commands and the `ai.*` commands the same way. No other process is started.

```
 AI panel / API / MCP bridge                Mikser Studio                                AI tools the user runs
 ──────────────────────────────────────  ─────────────────────────────────────────────  ──────────────────────
  mikserstudio.ollama.generate_text  ──►  the ollama plugin (Lua): a job of its own
                                            └─ http.request, http.stream_ndjson (C++)  ──────►  Ollama
  ai.run {op: image.generate}  ────────►  AiEngine: ComfyUiBackend (C++)  ──────────────────►  ComfyUI
                                            └─ on success: project.import_media, one undo step
  jobs.list (poll), jobs.cancel  ──────►  core's job manager: every job above
```

## The Ollama plugin

`plugins/builtin/ollama` (`mikserstudio.ollama`) has the `network` permission and the network entries
`127.0.0.1:*`, `localhost:*`, `[::1]:*` and `*:*`: Ollama may run on this computer or another one, on
any port, and the person sets its address. Its one setting, `url`, is the address, by default
`http://127.0.0.1:11434`; the Plugins panel changes it, and agents with `plugins.set_settings`. A job
uses the address that was set when it was asked for, so a new address applies to new jobs and running
ones go on with the old one.

| Command | Params | Result |
|---|---|---|
| `mikserstudio.ollama.generate_text` | `{prompt, model?}` | `{jobId}`; the job's result is `{text, model, provenance}` |
| `mikserstudio.ollama.models` | — | `{models: [{name, digest, family, parameterSize, quantization}]}`, read-only |

Both are `open_world` (the MCP bridge marks them `openWorldHint`) and need `network`.
`generate_text` answers at once and works in a job of the plugin's own, in the lane `generate`, so
one answer is written at a time:

- It asks Ollama for its version and models. Without `model` it uses the first installed model whose
  capabilities (`/api/show`) include `completion`, so vision-only and embedding models are skipped; a
  model that doesn't say what it can do is trusted. `model` may leave out `:latest`.
- It streams `/api/generate`, showing "Thinking with …" or "Writing with … (n tokens)" as the job's
  progress. The answer is the text after the model's last `</think>`, so a thinking model's reasoning
  is dropped. An answer that stops inside `<think>`, or one that has only reasoning when Ollama says
  it ran out of room, fails the job: "The model ran out of room before it answered; allow it more
  tokens or use a model that doesn't think first."
- The provenance names the plugin, the operation, the params, Ollama's version, the model's name,
  digest, family, size and quantization, and the time.
- Its failures say what to do: start Ollama, download a model with `ollama pull`, choose another
  model. When Ollama stops answering, its own message goes to the log.
- Canceling the job cancels its requests, which closes the connection and stops Ollama's answer.

The requests time out after 3 s (`/api/version`, `/api/tags`), 15 s (`/api/show`, which reads the
model file) and 600 s of silence (`/api/generate`: loading a large model can take minutes). Its specs
(`spec/`) run against the fake Ollama of `tests/services`.

## The ComfyUI plugin

`plugins/builtin/comfyui` (`mikserstudio.comfyui`) has the permissions `network`, `files` (to import
what ComfyUI made), `project.read` and `project.write`, and the same network entries as the Ollama
plugin. Its one setting, `url`, is ComfyUI's address, by default `http://127.0.0.1:8188`; as for
Ollama, a job uses the address that was set when it was asked for.

| Command | Params | Result |
|---|---|---|
| `mikserstudio.comfyui.generate_image` | `{prompt, negative_prompt = "", width = 1024, height = 1024, steps = 25, seed?}` | `{jobId}`; the job's result is `{mediaIds, seed, outputs, provenance}` |
| `mikserstudio.comfyui.check` | — | `{available, version?, reason?}`, read-only |

Both are `open_world` and need `network`. `generate_image` answers at once and works in a job of the
plugin's own, in the lane `generate`:

- It asks ComfyUI for its version (`/system_stats`), its models (`/models/<folder>`) and its nodes
  (`/object_info/<node>`), all at once, and names what is missing: "ComfyUI lacks …". `check` asks
  the same.
- The workflow is ComfyUI's own Qwen Image 2.1 text-to-image template without its prompt enhancer,
  as a Lua table (`workflow.lua`) that the plugin fills in: the prompts, the size rounded down to a
  multiple of 16, the steps and the seed (a new one when none is given). It uses only ComfyUI's local
  nodes, never its API nodes. It runs in a `comfyui.run_prompt` job ([plugins.md](plugins.md#job-kinds)),
  which shows the steps and the stage as the job's progress.
- ComfyUI saves into `mikserstudio/<instance>/<job id>` in its output folder, where `<instance>` is
  `app.info`'s name (Mikser Studio, Mikser Studio Dev or Mikser Studio E2E), so each instance's
  images stay apart. The job copies them into the app's cache before importing them.
- When the job has finished, the plugin imports each image with `project.import_media` and the
  provenance, inside one `ms.transaction` ("Add AI result"): one undo step, and a change the person
  makes meanwhile waits until it's done. The images are named after the start of the prompt. If the
  project was replaced (`project.session` changed) since the command, nothing is imported and the job
  says so.
- The provenance names the plugin, the operation, the params with the seed, the app's name and
  version, ComfyUI's version, the models, the method (workflow, sampler, scheduler, cfg, steps, seed
  and size) and the time, with what the job saw itself: the prompt's id and ComfyUI's address.
- Canceling the job takes its prompt off ComfyUI's queue if it hasn't started; a prompt that runs
  goes on in ComfyUI, since the plugin never interrupts ComfyUI or frees its memory.

Its specs run against the fake ComfyUI of `tests/services`. Until the AI engine goes, `ai.run`
still runs the same workflow from C++.

## The engine and its jobs

`ai.run` checks the parameters against the operation's JSON Schema, applies its defaults and queues a
job at once. It answers with the job id (`aijob-<uuid>`); `ai.jobs` then shows how the job goes. The
AI panel polls `jobs.list`, the core's list of every job, and shows the AI engine's jobs and the
plugins' own (such as the Ollama plugin's text jobs) that are part of no other job.

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
| `ai.status` | — | `{activeJobs, comfyuiUrl}`, read-only |
| `ai.settings` | — | `{comfyuiUrl}`, read-only |
| `ai.set_settings` | `{comfyuiUrl?}` | `{comfyuiUrl}` |

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
    "seed": 81723645012,             // the operation's own fields
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
| `comfyui` | `image.generate {prompt, negative_prompt = "", width = 1024, height = 1024, steps = 25, seed?}` | Runs ComfyUI's Qwen Image 2.1 text-to-image workflow, without its prompt enhancer, in the user's ComfyUI: the diffusion model `qwen_image_2.1_int8_convrot.safetensors` through `QwenImage21Cache`, the text encoder `qwen3vl_8b_int8_convrot.safetensors` with `TextEncodeQwenImage21`, KSampler (euler, simple, cfg 1), the VAE `qwen_image_2.1_vae_bf16.safetensors`, and `SaveImage` into `mikserstudio/<job id>` in ComfyUI's output folder. The image is then copied into the job's folder. Sizes are rounded down to a multiple of 16. Without `seed` it picks one and returns it as `{seed}`. Progress comes from ComfyUI's socket. Provenance: ComfyUI's version, the three models, and the sampler, steps, seed, size and ComfyUI's prompt id. Unavailable when ComfyUI is not running or lacks one of the models or nodes; the reason names them. |

The `comfyui` backend uses only ComfyUI's local nodes, never its API nodes (whose names end in
`Api`), which would send the work to an online service. It never calls `/interrupt` or `/free`, which
would stop or unload whatever ComfyUI runs for someone else. Canceling deletes only the job's own
queued prompt, with `POST /queue {"delete": [<prompt id>]}`; a prompt that already runs is left to
finish, and its image stays in ComfyUI's output folder.

## Settings

`ai-settings.json` in the instance's config folder holds ComfyUI's address (Ollama's is the Ollama
plugin's setting):

```json
{"comfyuiUrl": "http://127.0.0.1:8188"}
```

This is also the default. `ai.set_settings` changes it and saves the file; an `ollamaUrl` left in the
file from before is ignored. An address must be an `http` or `https` URL with a host; the error names
the setting. A new address
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
| ComfyUI | `/system_stats`, `/models`, `/object_info`, `/history`, `/queue` | 5 s |
| ComfyUI | `/prompt` (ComfyUI checks the whole workflow) | 30 s |
| ComfyUI | `/view`, per image | 120 s |
| ComfyUI | opening the socket (`/ws`) | 10 s |
| ComfyUI | a queued or running prompt | 10 min with no socket message: the prompt is taken off the queue and the job fails |

## Adding a backend

A new AI tool comes as a plugin, as Ollama did: its policy in Lua, on the app's job kinds
([plugins.md](plugins.md#how-a-feature-becomes-a-lua-plugin)). What follows is how the AI engine's
C++ backends work, for ComfyUI's until it moves too.

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

`tests/services` (Catch2) runs the engine, its backend and the commands against a fake ComfyUI in the
test process (`tests/services/FakeAiTools.*`), on an ephemeral loopback port. The fake answers the
parts of ComfyUI's API the backend uses and records what it was asked, so the tests check the requests
too, such as that canceling deletes only the job's own prompt. The Ollama plugin's specs run against
the fake Ollama of the same file, which a spec sets up over HTTP (`/fake/setup`).

`tests/e2e/AiRunTests.cpp` starts the built app headless, points it at a fake ComfyUI with
`ai.set_settings`, runs `ai.run image.generate` through the local API, and checks that the image comes
into the project with its provenance. So `ctest --preset e2e` covers the whole path without a real AI
tool.
