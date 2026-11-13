# Mikser Studio architecture

Mikser Studio is a native C++23 / Qt 6 desktop app with a headless core, a plugin system and AI that runs in
separate Python processes. Why this stack was chosen is in `stack-recommendation.md`.

## Principles

- **Everything is a command.** Every user-visible action is a `CommandSpec` (id, title, JSON Schema
  of its params) registered on the `CommandBus`. Menus, shortcuts, the command palette, the `/v1`
  API, macros and plugins all execute the same commands, and undo works the same for all of them.
- **The core is headless.** `src/core` depends only on Qt Core. The UI is a client of the core; it
  must never change the project except through commands.
- **Module ≠ process.** Code is split into modules everywhere; a separate process is used only for
  isolation or heavy resources (the AI service, export rendering).
- **Plugins first.** Adding a panel, a command or an AI backend must not need changes in the core.
  Plugin settings are JSON Schema and the UI generates forms from them.
- **Interfaces at the seams.** The docking library (`ui::DockHost`), the media engine
  (`core::Transport`, `core::MediaProbe`) and AI backends sit behind small interfaces so each can be
  replaced (e.g. MLT's CPU compositor by our own GPU compositor later).

## Modules

| Folder | Target | Depends on | Job |
|---|---|---|---|
| `sdk/` | `MikserStudio::sdk` | — | Public C ABI for native plugins (`plugin_api.h`, MIT). |
| `src/core/` | `MikserStudio::core` | Qt Core | Command bus, undo, JSON Schema validation, project model, transport interface, plugin manager. |
| `src/engine/` | `MikserStudio::engine` | core, MLT 7, Qt Gui | MLT runtime, media probe, playback engines (source clip, program timeline), GPU monitor (QRhi). |
| `src/api/` | `MikserStudio::api` | core, Qt HttpServer/WebSockets | Versioned local API on 127.0.0.1: `/v1` HTTP + WebSocket events. |
| `src/services/` | `MikserStudio::services` | core, Qt Network | Sidecar process manager and the AI bridge (talks to `ai/`). |
| `src/ui/` | `MikserStudio::ui` | core, Qt Widgets/Quick, KDDockWidgets | Main window, docking, workspaces, panels (media bin, timeline, AI, palette, properties, log). |
| `src/app/` | `mikser-studio` | all | Wires the modules together; `mikser-studio --headless` runs without UI. |
| `plugins/` | plugin libraries | sdk | Bundled example plugins. |
| `ai/` | Python package `mikserstudio-ai` | — | AI sidecar service, run from its own virtual environment (`ai/.venv`); it connects to the AI tools the user runs and runs no models itself. |
| `tests/` | `*-tests` | Catch2 | Tests mirroring `src/` and `plugins/`, and the end-to-end tests. |

## Import directions

Code includes and imports only in these directions, so each module can be built, tested and replaced
on its own. `tools/check_includes.py` checks the C++ and the import-linter contracts in
`ai/pyproject.toml` the Python; the `Checks` workflow runs both.

- `sdk/` includes nothing of Mikser Studio: it is the plugin ABI, a plain C header.
- `src/core/` includes only itself and the plugin ABI.
- `src/engine/`, `src/api/`, `src/services/` and `src/ui/` include only themselves and `core`. The UI
  never includes the engine: the app creates the engine's monitor panels and hands them to the UI,
  and the UI controls playback through `core::Transport`.
- `src/app/` includes every module, and nothing includes `src/app/`.
- `plugins/` include only the plugin ABI.
- A module's public headers are included in quotes as `"mikserstudio/<module>/..."`. Its private
  headers are included in quotes by name, and only by the module itself and its tests in
  `tests/<module>/`. The plugin ABI is included in angle brackets, `<mikserstudio/plugin_api.h>`, as
  a plugin built apart from Mikser Studio includes it.
- `tests/e2e/` includes only the core and the services: the end-to-end tests run the app as a
  process, and the real AI service through the services module. `tests/plugins/` includes only the
  core, whose PluginManager loads the bundled plugins as the app does. Both may use the support files
  of those modules' tests. Every C++ file is in a folder the check knows, so a new folder has to be
  added to it before its includes pass.
- In `ai/`, the service (`service`, `server`, `jobs`, `registry` and the modules they use) never
  imports `backends/`: backends are found through the `mikserstudio_ai.backends` entry points.
  Backends use only the backend contract (`backend.py`) and the shared helpers (`processes.py`,
  `provenance.py`, `logs.py`), and never each other. The service is layered: `__main__`, `service`,
  `server`, then `jobs`, `registry`, `auth` and `params`, then `backend`; each imports only from the
  layers below it.

## Terms

Each concept has one name, used in the interface, the code, file names, storage keys, docs and commit
messages.

| Term | Meaning | Not |
|---|---|---|
| project | What the user works on: the media bin and the timeline, saved as a `.mikser` file. | document, session |
| media item | A file imported into the project, listed in the media bin (`project.import_media`). | asset, source file |
| media bin | The panel and list of the project's media items. | library, pool |
| clip | A piece of a media item placed on a track of the timeline. | item, segment |
| track | A row of the timeline holding clips: video tracks `V1`, `V2`, audio tracks `A1`, `A2`. | lane, layer |
| timeline | The project's tracks and their clips, in time. | sequence |
| playhead | The current frame of the timeline. | cursor, position marker |
| source monitor | The monitor that plays one media item. | clip viewer |
| program monitor | The monitor that plays the timeline. | preview, output |
| panel | A movable, dockable part of the main window (media bin, timeline, AI, log). | dock, pane, widget |
| workspace | A saved arrangement of the panels. | layout, perspective (in the code, *layout* is only KDDockWidgets' saved form of a workspace, as in `LayoutSaver` and `layout.json`) |
| command | An action with an id such as `project.import_media`, run the same way from menus, shortcuts, the command palette, the API and plugins. | action, operation |
| command palette | The popup that finds and runs commands (Ctrl+K). | quick open |
| plugin | A shared library with a `mikserstudio-plugin.json` manifest that adds commands, settings and panels. | extension, add-on, module |
| local API | The versioned `/v1` HTTP and WebSocket API on `127.0.0.1`. | server, remote control |
| instance | Which Mikser Studio runs: the installed app, Mikser Studio Dev or Mikser Studio E2E, each with its own name, app id and folders (`MIKSERSTUDIO_INSTANCE`). | profile, variant |
| discovery file | `api.json`, which tells other programs the local API's address and session token. | config file |
| session token | The secret a client needs for the local API or the AI service, made anew at each start. | key, password |
| AI service | The Python process in `ai/` whose backends connect to the AI tools the user runs; *sidecar* in the code that manages it (`SidecarManager`). | AI server, worker |
| backend | A part of the AI service that offers operations, such as `builtin` and `ollama`. | provider, engine |
| operation | What a backend can do, such as `video.upscale` or `text.generate`. | task, op type |
| job | One run of an operation, with a state: `queued`, `running`, `succeeded`, `failed` or `canceled`. | task, request |
| provenance | The record of how AI made a media item: the job, backend, operation, model, parameters, time and tool versions. | metadata, history |

## Threads and processes

- **Main thread:** UI, core, command execution, project model.
- **MLT threads:** decoding and the RtAudio consumer. The audio device clock is the master clock:
  video frames are shown when the audio says so. Frames reach the monitor through
  `engine::FrameSink` without going through the Qt event queue.
- **Monitor:** a QRhi-backed `QWindow` (D3D11 on Windows, Metal on macOS, Vulkan/GL on Linux)
  embedded with `QWidget::createWindowContainer`, so playback never takes the extra copy that
  `QRhiWidget`/`QQuickWidget` make.
- **AI sidecar:** `mikserstudio-ai` (Python, aiohttp) started on demand by `services::SidecarManager`, on
  127.0.0.1 with a session token. Generated media is written to the project cache with provenance.

## Conventions

- C++23, Qt 6.8+ (developed on 6.12). `QT_NO_KEYWORDS` is on: write `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`.
  `QT_NO_CAST_FROM_ASCII` is on: wrap literals in `u"..."_s` (`using namespace Qt::StringLiterals`)
  or `QStringLiteral`.
- Naming: the product is **Mikser Studio** (only in text people read). Code uses `mikserstudio`
  (namespaces, plugin and panel ids, the Python package `mikserstudio_ai`), `MikserStudio::` for
  CMake targets and `MIKSERSTUDIO_` for macros and environment variables. Programs people run are
  `mikser-studio*`. Projects are `.mikser` files. The app id is `io.github.infinitinker.mikserstudio`
  (`kAppId` in `mikserstudio/core/Instance.h`); it must never change after a release.
- Namespaces follow folders: `mikserstudio::core`, `mikserstudio::engine`, `mikserstudio::ui`, ... Public headers live in
  `<module>/include/mikserstudio/<module>/`, private code in `<module>/src/`.
- Errors are explicit: return `std::expected<T, QString>` (or `CommandError`) with an English message
  that tells the user what went wrong and what to do. Never swallow errors; use `QLoggingCategory`
  (`mikserstudio.<module>`), not `qDebug` without a category.
- Small files with one job each. No dead or commented-out code, no speculative abstractions.
- All internal libraries are static; native plugins are shared libraries loaded through the C ABI.
- Tests use Catch2 v3 and run without a GPU, network or audio device (use MLT's `null` consumer and
  `color:` producers, fake transports, ephemeral ports).
