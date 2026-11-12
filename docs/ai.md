# AI in MikserStudio: the sidecar and the AI bridge

AI runs outside the app, in a Python process called the **sidecar** (`ai/`, package `mikserstudio-ai`).
The app talks to it over a small HTTP API on `127.0.0.1`. The C++ side lives in `src/services`
(`MikserStudio::services`): it starts and supervises the sidecar, talks to its API and offers AI features as
ordinary commands (`ai.*`) on the `CommandBus`, so menus, the command palette, macros, plugins and
the `/v1` API all use them the same way.

```
 UI / API / macros                  Mikser Studio app (C++)                          mikserstudio-ai (Python)
 ─────────────────   ai.* commands  ─────────────────────────────────  HTTP  ──────────────────────
  ai.upscale_clip  ───────────────►  AiJobTracker ── AiClient ───────────────►  /v1 API (aiohttp)
  ai.jobs (poll)   ◄───────────────   │   polls GET /v1/jobs every 0.5 s       JobManager (queue per
                                      │                                          backend, cancel)
                                      ├── on success: project.import_media       BackendRegistry
                                      │   (+ provenance) + timeline.add_clip      ├─ builtin (ffmpeg)
  SidecarManager ── QProcess ─────────┘   (one undo step)                         └─ ollama  (HTTP)
   start / READY line / health / restart with backoff / stop (stdin closed → terminate → kill)
```

Why a separate process: models are big, crash-prone and Python-first (ComfyUI, diffusers, Ollama
clients). A crash or a hung model never takes the editor down; the manager restarts the sidecar and
fails only the jobs it was running.

## Running the sidecar

The app starts it on demand (the first AI command) with:

```
<ai dir>/.venv/Scripts/mikserstudio-ai --host 127.0.0.1 --port 0 --cache-dir <dir> --exit-with-stdin [--ffmpeg <path>]
```

and the session token in the environment variable `MIKSERSTUDIO_AI_TOKEN` (`--token <t>` works too, but
command lines can be read by other processes). `mikserstudio-ai` is the program pip installs into the
virtual environment `ai/.venv` (`bin/mikserstudio-ai` there on Linux and macOS), which is set up as
[development.md](development.md) describes; without it the app reports how to create it. Nothing creates
the environment for an installed copy yet: an installer will have to ship it.

When it listens, the sidecar prints exactly one line on stdout:

```
MIKSERSTUDIO_AI_READY port=52341
```

Logs go to stderr (the app forwards them to the `mikserstudio.services.sidecar` log category). The sidecar
stops when its stdin is closed — that is how the app stops it on every OS, and it also means the
sidecar never outlives a crashed app. If it has not stopped 10 s after stdin closed (a hung job), it
exits anyway. `SIGTERM`/`SIGINT` stop it where the OS has them.

The app starts it in the cache folder with `NoDefaultCurrentDirectoryInExePath=1`, and the sidecar
looks up ffmpeg/ffprobe only in absolute `PATH` folders (never in its working directory, which
Windows would search first), so a program planted in the folder Mikser Studio was started from (e.g.
Downloads) cannot stand in for ffmpeg.

| Option | Default | Meaning |
|---|---|---|
| `--host` | `127.0.0.1` | Loopback only (`127.0.0.1`, `::1`, `localhost`); anything else is refused. |
| `--port` | `0` | `0` picks a free port, reported in the READY line. |
| `--token` | `$MIKSERSTUDIO_AI_TOKEN` | Required one way or the other. |
| `--cache-dir` | — | Required. Job outputs go to `<cache-dir>/<job id>/`. |
| `--ffmpeg` | `ffmpeg` on `PATH` | ffmpeg for the builtin backend (an absolute path); ffprobe is looked up next to it. |
| `--ollama-url` | `http://127.0.0.1:11434` | Base URL of Ollama. |
| `--exit-with-stdin` | off | Stop when stdin closes (the app always passes it). |
| `--log-level` | `INFO` | `DEBUG`, `INFO`, `WARNING`, `ERROR`. |

Development: in `ai/`, `.venv/Scripts/python -m pytest` (tests need no GPU, network or Ollama; the
upscale tests are skipped without ffmpeg). `.venv/Scripts/python -m ruff check .` and
`.venv/Scripts/python -m mypy` must stay clean.

## HTTP API (`/v1`)

JSON in and out. Every route except `GET /v1/health` needs `Authorization: Bearer <token>`
(compared in constant time; `401` + `WWW-Authenticate: Bearer` otherwise). Errors use the same shape
as the Mikser Studio `/v1` API:

```json
{"error": {"code": "invalid_params", "message": "Invalid parameters for \"video.upscale\": scale: must be one of 2, 3, 4"}}
```

| Route | Answer |
|---|---|
| `GET /v1/health` | `{"status": "ok", "service": "mikserstudio-ai", "version": "0.1.0"}` |
| `GET /v1/backends` | `[{id, title, ops: [{id, title, description, paramsSchema}], available, reason}]` |
| `POST /v1/jobs` `{op, params, backend?}` | `202 {"jobId": "job-…"}` |
| `GET /v1/jobs` | Every job, oldest first (the last 200 finished ones are kept). |
| `GET /v1/jobs/{id}` | One job. |
| `DELETE /v1/jobs/{id}` | Cancels a queued or running job, waits until it stopped, answers the job. |

`POST /v1/jobs` picks the first **available** backend that offers `op` (or the one named in
`backend`), applies the schema defaults and validates `params`. Errors: `400 invalid_params`,
`400 malformed_json`, `404 unknown_op`, `409 unavailable` (with the backend's reason, e.g. "Ollama is
not running…"). `DELETE` answers `409 already_finished` for a finished job and `404 not_found` for an
unknown one.

A job:

```json
{
  "id": "job-0f…", "op": "video.upscale", "backend": "builtin",
  "params": {"input": "D:/media/clip.mp4", "scale": 2},
  "state": "succeeded",            // queued | running | succeeded | failed | canceled
  "progress": 1.0,                 // 0..1
  "message": "Done",               // short status for the UI
  "createdAt": "2026-11-11T01:08:19Z", "startedAt": "…", "finishedAt": "…",
  "result": {
    "outputs": [{"path": "C:/…/cache/job-0f…/clip_x2.mkv", "kind": "video"}],
    "provenance": {
      "jobId": "job-0f…", "backend": "builtin", "op": "video.upscale",
      "params": {"input": "D:/media/clip.mp4", "scale": 2},
      "createdAt": "2026-11-11T01:08:31Z",
      "service": {"name": "mikserstudio-ai", "version": "0.1.0"},
      "tools": {"ffmpeg": "9.0.2"},
      "method": {"ai": false, "filter": "scale=iw*2:ih*2:flags=lanczos", "encoder": "libx264 crf 16"}
    },
    "width": 3840, "height": 2160    // op-specific fields
  },
  "error": null                     // English message when failed
}
```

Jobs of one backend run one at a time (GPU models do not share well); jobs of different backends run
side by side. Output kinds: `video`, `image`, `audio`, `text`.

## Backends

| Backend | Operation | What it does |
|---|---|---|
| `builtin` | `video.upscale {input, scale = 2 \| 3 \| 4}` | ffmpeg lanczos scaling, H.264 (libx264, CRF 16) + copied audio in `.mkv`. **Not AI**: a placeholder that exercises the whole pipeline (queue, progress from `ffmpeg -progress`, cancel, cache, provenance, import) until a ComfyUI backend provides a real AI upscaler. Refuses outputs larger than 8K. Unavailable without ffmpeg/ffprobe/libx264, with the reason. |
| `ollama` | `text.generate {prompt, model?}` | Streams `/api/generate` from a local Ollama. Without `model` it uses the first installed model whose capabilities include `completion` (vision-only or embedding models are skipped). Unavailable when Ollama is not running or has no models. Result: `{text, model}` plus `response.txt`; reasoning that a thinking model writes before `</think>` is dropped from `text`. Provenance has the Ollama version and the model name, digest, family, size and quantization. |

### Adding a backend

A backend is one module (or package) plus one entry point; nothing else changes.

1. Create `ai/src/mikserstudio_ai/backends/<name>/__init__.py` (or a module elsewhere, even in another
   package) with a class that satisfies `mikserstudio_ai.backend.Backend`:

   ```python
   class ComfyBackend:
       id = "comfyui"            # property or attribute
       title = "ComfyUI"
       def operations(self) -> Sequence[Operation]: ...       # ids, titles, JSON Schemas
       async def availability(self) -> Availability: ...       # cheap; give a reason the user can act on
       async def run(self, op, params, context: JobContext) -> Outcome: ...
       async def close(self) -> None: ...

   def create(settings: Settings) -> Backend:
       return ComfyBackend(...)
   ```

2. Register it in `ai/pyproject.toml`:

   ```toml
   [project.entry-points."mikserstudio_ai.backends"]
   comfyui = "mikserstudio_ai.backends.comfyui:create"
   ```

   (Run `.venv/Scripts/python -m pip install -e ".[dev]"` in `ai/` again so the environment sees the new entry point.) A backend
   that fails to import
   or create is logged and skipped; the others keep working.

Rules for `run()`:

- `params` already passed the operation's schema with defaults applied (supported keywords: `type`,
  `enum`, `minimum`, `maximum`, `minLength`, `maxLength`, `properties`, `required`,
  `additionalProperties: false`, `default`).
- Write outputs only into `context.output_dir` and return them as `Output(path, kind)`.
- Report progress with `context.report(fraction, message)`.
- Raise `BackendError("…")` with a message for the user for expected failures.
- Cancellation arrives as `asyncio.CancelledError`: kill child processes (see
  `mikserstudio_ai.processes.kill`), delete partial outputs, re-raise.
- Return provenance in `Outcome.provenance` (`tools` with versions, `model` with name and hash, seed,
  workflow…). The job manager adds `jobId`, `backend`, `op`, `params`, `createdAt` and the service
  version. Start child processes with `creationflags=NO_WINDOW` so no console windows pop up on
  Windows.

An operation id can be offered by several backends (e.g. a future `comfyui` `video.upscale`); the
first available one in entry-point order wins unless the client names a `backend`.

## The C++ side (`src/services`)

- `services::SidecarManager` — owns the process. States: `Stopped → Starting → Running`, and
  `Restarting` (backoff `restartDelay × 2^n`) after a crash, a missing READY line within
  `readyTimeout`, or `healthFailuresBeforeRestart` failed `GET /v1/health` checks; `Failed` after
  `maxRestarts` (the budget refills after `stableAfter` of trouble-free running; `start()` from
  `Failed` retries). A fresh 32-byte token per launch. `stop()` closes stdin, waits `stopTimeout`,
  then terminates, then kills. User-facing errors come through `errorOccurred` and `errorString()`,
  e.g. *"The AI service could not start: its Python environment "…\ai\.venv" was not found. Create
  it as docs/development.md describes and restart Mikser Studio."*
- `services::AiClient` — async `QNetworkAccessManager` client of the API above (no proxy, transfer
  timeout, bearer token). Each call answers once through a callback guarded by a context object.
- `services::registerAiCommands(bus, project, sidecar, client)` — the commands below. Jobs get a
  local id (`aijob-…`) at once, wait for the sidecar (which is started on demand), are submitted and
  then polled every 0.5 s while active. A sidecar crash fails the jobs it was running ("The AI
  service stopped while the job was running. Start the job again.").

| Command | Params | Result |
|---|---|---|
| `ai.status` | — | `{state, error, baseUrl, restarts, activeJobs}` |
| `ai.backends` | — | `{backends: [...]}`; `not_available` while the service starts or has failed |
| `ai.jobs` | — | `{jobs: [{id, remoteId, op, title, params, state, progress, message, error, createdAt, result, placement}]}` |
| `ai.cancel_job` | `{jobId}` | `{jobId, state}` |
| `ai.upscale_clip` | `{clipId, scale = 2}` | `{jobId}` |
| `ai.generate_text` | `{prompt, model?}` | `{jobId}`; the answer is in the job's `result.text` |

`ai.upscale_clip` checks at once that the clip is a video clip, then runs `video.upscale` on the
clip's media. When the job succeeds it, through the bus and as **one** undo step ("Place AI
Result"):

1. imports the output with `project.import_media` (name `"<source> (upscaled x2)"`) and its
   `provenance` param: the job provenance plus `sourceClipId` and `sourceMediaId`,
2. adds a video track on top with `timeline.add_track` when the clip is on the top video track,
3. adds the result with `timeline.add_clip` on the video track above the clip (V1 → V2, V2 → a new
   V3), at the source clip's position and with its in/out range — job `placement` =
   `{mediaId, clipId, trackId}`.

One `edit.undo` removes all of it. If the clip was removed or the target range is taken by then,
the media stays imported (the same undo step removes it) and the job fails with a message saying
so. A job whose project was replaced in the meantime (`project.new`, `project.open`) fails with
"The project was replaced before the upscale finished..." and changes nothing; its output stays in
the cache folder.
