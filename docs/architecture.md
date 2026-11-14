# Mikser Studio architecture

Mikser Studio is a native C++23 / Qt 6 desktop app: a C++ core with most features in Lua 5.4. The C++
side keeps what must be fast or close to the system: the media engine, the shell and the command bus.
The features run in Lua as built-in plugins that use the same public API as anyone else's plugins.
Agents and scripts outside the app drive it through the local API and the MCP bridge. A release ships
no Python. Why this stack was chosen is in `stack-recommendation.md`.

The Lua side is being built now. Each part below is marked **built** (in the code today) or **planned**
(designed here, not in the code yet), so this document never claims what doesn't exist.

## Principles

- **Everything is a command.** Every user-visible action is a `CommandSpec` (id, title, JSON Schema
  of its params) registered on the `CommandBus`. Menus, shortcuts, the command palette, the `/v1`
  API, the MCP bridge, macros and Lua plugins all execute the same commands, and undo works the same
  for all of them. (built; Lua planned)
- **Features are built-in Lua plugins.** A feature such as the Ollama or ComfyUI connection is a Lua
  plugin shipped with the app, using only the public Lua API: commands, events, jobs and read-only
  project views. A third-party plugin has the same API and the same sandbox. (planned)
- **Heavy work stays in C++.** Decoding, rendering, real-time audio, network streaming and file
  imports run as C++ jobs that Lua starts and awaits. Lua never runs on the UI thread and never
  blocks it. (planned)
- **The core is headless.** `src/core` depends only on Qt Core. The UI is a client of the core; it
  must never change the project except through commands. (built)
- **Module ≠ process.** Code is split into modules everywhere; a separate process is used only for
  isolation or heavy resources (export rendering, the MCP bridge). (built)
- **Described UI.** Plugins describe their UI as data: JSON Schema forms, menus, panels with lists
  and buttons. The shell draws it. Only built-in plugins may bring a custom QML panel. (planned)
- **Interfaces at the seams.** The docking library (`ui::DockHost`), the media engine
  (`core::Transport`, `core::MediaProbe`) and the plugin runtime sit behind small interfaces, so each
  can be replaced (e.g. MLT's CPU compositor by our own GPU compositor later). (built; the plugin
  runtime seam planned)

## Layers

| # | Layer | Language | What it holds | State |
|---|---|---|---|---|
| 1 | Engine | C++ | Decoding and encoding (FFmpeg, MLT), the render graph, GPU drawing (QRhi; OpenColorIO and scopes later), the real-time audio thread, the project and timeline model with undo. | built (OCIO, scopes planned) |
| 2 | Shell | C++/Qt | The main window and docking, the monitors, the timeline widget: the UI that must stay smooth. | built |
| 3 | Command bus | C++ | Commands with schemas, events and async jobs. `/v1` and the MCP bridge sit on it. | built (events and jobs in core planned) |
| 4 | Lua host | C++ | Lua 5.4.9 through sol2 3.5.0. One state per plugin, each on its own worker thread. Permissions, sandbox, hot reload. | planned |
| 5 | App layer | Lua | Most features, shipped as built-in plugins that use only the public API. | planned |
| 6 | Plugins | Lua | Third-party and user plugins: the same API, sandboxed. | planned |
| 7 | Effects | GLSL | Text files compiled at run time with Qt ShaderTools. | planned |
| 8 | Outside | any | Scripts and agents through `/v1` and MCP. | built |

The rules between them:

- Lua reaches only commands, events and read-only project views, never engine internals.
- Heavy work is a C++ job that Lua starts and awaits.
- Lua never runs on, or blocks, the UI thread.
- UI from Lua is described: JSON Schema forms, menus, panels. QML only for custom panels of built-in
  plugins (and the example plugin).

## Modules

| Folder | Target | Depends on | Job | State |
|---|---|---|---|---|
| `src/core/` | `MikserStudio::core` | Qt Core | Command bus, undo, JSON Schema validation, project model, transport interface, plugin manager (discovery, manifests, settings). | built |
| `src/engine/` | `MikserStudio::engine` | core, MLT 7, Qt Gui | MLT runtime, media probe, playback engines (source clip, program timeline), GPU monitor (QRhi). | built |
| `src/api/` | `MikserStudio::api` | core, Qt HttpServer/WebSockets | Versioned local API on 127.0.0.1: `/v1` HTTP + WebSocket events. | built |
| `src/lua/` | `MikserStudio::lua` | core, Lua 5.4, sol2 | The Lua host: one sandboxed state per plugin on its own thread, the `ms` API, permissions, hot reload. It implements core's plugin runtime interface. | planned |
| `src/services/` | `MikserStudio::services` | core, Qt Network/WebSockets | The AI engine today: its backends connect to the AI tools the user runs (Ollama, ComfyUI), and its `ai.*` commands run their operations as jobs. Its network parts become C++ job kinds and its policy moves into the `ollama` and `comfyui` plugins. | built, to be replaced |
| `src/ui/` | `MikserStudio::ui` | core, Qt Widgets/Quick, KDDockWidgets | Main window, docking, workspaces, panels (media bin, timeline, AI, palette, properties, log). | built |
| `src/app/` | `mikser-studio` | all | Wires the modules together; `mikser-studio --headless` runs without UI. | built |
| `src/mcp/` | `mikser-studio-mcp` | Qt Network/WebSockets | The MCP bridge, a program of its own: offers the commands to agents as MCP tools through the local API. | built |
| `plugins/` | Lua plugins | the Lua API | Built-in plugins in `plugins/builtin/<name>/`, the example in `plugins/examples/hello/`. | planned (today: the native `plugins/hello/`) |
| `effects/` | GLSL effects | — | One folder per effect: `effect.frag` and `effect.json`. | planned |
| `sdk/` | `MikserStudio::sdk` | — | The C ABI of native plugins (`plugin_api.h`, MIT). Native plugins go away when the Lua host is in, and `sdk/` with them. | built, to be removed |
| `tools/` | development scripts | — | The release build, the third-party notices, the license and include checks and what they read from MSYS2, in Lua 5.4 with LuaFileSystem and lua-cjson, with their tests in `tools/tests/`; never shipped. Checked with StyLua and selene, with the settings in `tools/stylua.toml` and `tools/selene.toml`. | built |
| `tests/` | `*-tests` | Catch2 | Tests mirroring `src/` and `plugins/`, and the end-to-end tests. | built |

## Import directions

Code includes and imports only in these directions, so each module can be built, tested and replaced
on its own. `tools/check_includes.lua` checks them, and the `Checks` workflow runs it.

- `src/core/` includes only itself (and, until native plugins go, the plugin ABI).
- `src/engine/`, `src/api/`, `src/services/` and `src/ui/` include only themselves and `core`. The UI
  never includes the engine: the app creates the engine's monitor panels and hands them to the UI,
  and the UI controls playback through `core::Transport`.
- `src/lua/` includes only itself and `core` (planned). Lua code sees only commands, events and
  project views: it never reaches a C++ object.
- `src/app/` includes every module, and nothing includes `src/app/`.
- `src/mcp/`, the MCP bridge, includes only itself and links no other module: it drives the app only
  through its local API, as any other program can. Nothing includes it.
- A module's public headers are included in quotes as `"mikserstudio/<module>/..."`. Its private
  headers are included in quotes by name, and only by the module itself and its tests in
  `tests/<module>/`.
- `tests/e2e/` includes only the core: the end-to-end tests run the app and the MCP bridge as
  processes, and use the services tests' fake AI tools. Every C++ file is in a folder the check
  knows, so a new folder has to be added to it before its includes pass.
- Until they are removed: `sdk/` includes nothing of Mikser Studio, native plugins in `plugins/`
  include only the plugin ABI, in angle brackets (`<mikserstudio/plugin_api.h>`), and
  `tests/plugins/` includes only the core.

## Terms

Each concept has one name, used in the interface, the code, file names, storage keys, docs and commit
messages.

| Term | Meaning | Not |
|---|---|---|
| project | What the user works on: the media bin and the timeline, saved as a `.msproj` file. | document, session |
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
| plugin | A folder with a `mikserstudio-plugin.json` manifest that adds commands, settings and panels. Today a native shared library; planned: a Lua plugin. | extension, add-on, module |
| Lua plugin | A plugin whose code is Lua (`entry` is a `.lua` file), run by the Lua host in a sandbox. | script, mod |
| built-in plugin | A Lua plugin shipped with the app in `plugins/builtin/`, granted its permissions automatically. Most features are built-in plugins. | core plugin, bundled extension |
| Lua host | The C++ module (`src/lua`) that runs Lua plugins: one state per plugin, each on its own thread. | interpreter, runtime, VM |
| permission | A right a plugin declares in its manifest and the person grants, such as `project.write` or `network`. | capability, scope |
| project view | An immutable snapshot of the project with a revision number, which Lua reads (`ms.project()`). | model, document object |
| call context | Who runs a command: its origin (`ui`, `api` or `plugin:<id>`, kept as a chain), the permissions it carries and whether the person confirmed it. | caller info, session |
| effect | A GLSL shader in `effects/<id>/` with its `effect.json`, compiled at run time. | filter, shader plugin |
| local API | The versioned `/v1` HTTP and WebSocket API on `127.0.0.1`. | server, remote control |
| MCP bridge | The program that offers the commands to agents as MCP tools, through the local API (`mikser-studio-mcp`). | MCP server, agent API |
| instance | Which Mikser Studio runs: the installed app, Mikser Studio Dev or Mikser Studio E2E, each with its own name, app id and folders (`MIKSERSTUDIO_INSTANCE`). | profile, variant |
| discovery file | `api.json`, which tells other programs the local API's address and session token. | config file |
| session token | The secret a client needs for the local API, made anew at each start. | key, password |
| AI engine | The part of the app that runs AI jobs in the AI tools the user runs, through its backends (`services::AiEngine`). Being replaced by the `ollama` and `comfyui` plugins. | AI service, sidecar |
| backend | A part of the AI engine that connects to one AI tool's API and offers operations, such as `ollama` and `comfyui`. | provider |
| operation | What a backend can do, such as `image.generate` or `text.generate`. | task, op type |
| job | One run of long work, with a state: `queued`, `running`, `succeeded`, `failed` or `canceled`. Today an AI job; planned: any job kind in core's job manager (`http.request`, `comfyui.run_prompt`, …), which Lua starts and awaits. | task, request |
| provenance | The record of how AI made a media item: the job, backend, operation, model, parameters, time and tool versions. | metadata, history |

## Threads and processes

- **Main thread:** UI, core, command execution, project model. (built)
- **Events:** `core::EventBus`, owned by the command bus, publishes each event (a topic, a JSON
  payload with the ids of what changed, and the origins of the command that caused it) on the main
  thread. `/v1/events` is one subscriber; a subscriber on another thread, such as a Lua plugin,
  reads through a bounded `core::EventInbox` that coalesces `transport.state` and `job.progress`.
  (built)
- **Commands that answer later:** a command marked `async` hands its handler a `done` callback
  instead of returning. `CommandBus::executeAsync` can be called from any thread: it queues the run
  to the main thread and the answer to the caller's. Menus, the palette, shortcuts, plugin panels and
  `/v1` use it; `/v1` stops waiting after 60 seconds. A synchronous `execute` of such a command
  answers `not_available`. (built)
- **MLT threads:** decoding and the RtAudio consumer. The audio device clock is the master clock:
  video frames are shown when the audio says so. Frames reach the monitor through
  `engine::FrameSink` without going through the Qt event queue. (built)
- **Monitor:** a QRhi-backed `QWindow` (D3D11 on Windows, Metal on macOS, Vulkan/GL on Linux)
  embedded with `QWidget::createWindowContainer`, so playback never takes the extra copy that
  `QRhiWidget`/`QQuickWidget` make. (built)
- **AI engine:** `services::AiEngine` runs on the main thread. Its requests to the AI tools are
  asynchronous and each has a timeout, so a slow or hung tool never blocks the interface. Generated
  media is written to the AI cache and imported with its provenance. (built, to be replaced)
- **Lua states:** one worker thread per Lua state, none on the UI thread; a state never leaves its
  thread. A call from Lua to the bus is queued to the main thread, and its result comes back queued
  to the state's thread, which resumes the waiting coroutine. Values cross threads only as JSON
  copies. (planned)
- **Jobs:** long work runs as a job in `core::JobManager`: jobs of one lane run one at a time, in
  order, and lanes run side by side; each has an owner, progress, cancel and a result, and the last
  200 finished ones are kept. The AI engine's jobs are mirrored onto it until its backends move to
  plugins. (built) Work runs off the main thread where it can, and Lua awaits a job without holding
  its thread. (planned)

## Undo for groups of changes

Today a group of changes is one global depth counter in `core::UndoStack` (`UndoGroup`): edits made
while a group is open become one undo step. (built)

A Lua transaction spans hops between threads, so with a counter alone a person's edit could land in
the middle of it, and undoing the group would revert edits the person's change was built on.
Transactions therefore take a **write lease** (planned):

- While a transaction is open, write commands from other origins wait, up to a deadline of about 2 s.
- Awaiting a job inside a transaction is an error.
- A Lua error or the deadline rolls the transaction back: its edits are undone in reverse order,
  instead of committing half of it.
- Each undo step records its origin.

## The Lua API, version 1 (planned)

The contract the Lua host builds. A plugin's code sees one global, `ms`.

- `ms.command{id, title, params, result, permission, handler}` registers a command. `params` and
  `result` are JSON Schemas; `permission` is the permission a caller needs to run it. The handler is
  `function(params, ctx)` and runs as a coroutine, so it can wait without blocking anything.
- `ms.run(id, params)` runs a command and yields until the bus answers. It returns the result, or
  raises an error with a `code`: `needs_confirmation`, `permission_denied`, `canceled`,
  `invalid_params` or `failed`.
- `ms.jobs.start(kind, input)` starts a C++ job and returns a job with `:await()` and `:cancel()`.
- `ms.on(topic, fn)` subscribes to events; each callback runs in its own coroutine.
- `ms.project()` returns an immutable project view with a revision number and queries (by clip id,
  range and media id).
- `ms.transaction(name, fn)` runs `fn` as one undo step (see "Undo for groups of changes").
- `ms.settings()` returns the plugin's settings, `ms.log` writes to the log under the plugin's id,
  and `ms.ui.confirm(question)` asks the person on the main thread and confirms that one call.
- `ms.transaction`, the dispatch of `ms.on` and anything else that calls back into Lua and may yield
  are written in a Lua prelude, not as C++ functions: sol2 has no continuations, and a yield across a
  C call fails.

### Permissions

- `project.read`: read the project through project views and read-only commands.
- `project.write`: run commands that change the project.
- `files`: open, save or import a path outside the project; the person grants it.
- `network`: connect to the `host:port` entries the manifest lists; no loopback by default, never
  the app's own API port, and settings can't widen the list.
- `ui`: contribute menus, panels and forms, ask the person (`ms.ui.confirm`), and drive the monitors
  and playback.
- `plugins.manage`: run the `plugins.*` commands; only built-in plugins get it.

The permission a command needs belongs to its caller. The call context keeps the chain of origins
(agent → plugin → command), and a plugin that calls another plugin's command needs that command's
permission itself. The bus checks it before the handler runs and answers `permission_denied`
(built: `core::CallContext`). The commands a handler runs keep its caller's origins and
permissions, but not the person's agreement, and runs nest at most 16 deep, which stops a loop.
A plugin never agrees for the person: a command with a `confirmation` that a plugin runs answers
`needs_confirmation` with the question, a `confirmationParam` the plugin sets is dropped, and only
`ms.ui.confirm` agrees, for that one call (built: `CallContext::confirmed`). The API and the MCP
bridge keep their contract: the API runs such commands without asking, and the bridge asks the
person first.

### Ids

- Plugin ids keep today's rule: dotted lower_snake, at least two segments. Built-in plugins are
  `mikserstudio.<name>` (`mikserstudio.ollama`).
- A plugin's command ids start with its id (`mikserstudio.ollama.generate_text`, which the MCP
  bridge offers as `mikserstudio_ollama_generate_text`).
- A command id stays within MCP's 64 characters after `.` becomes `_`, and two ids that map to the
  same tool name are refused. `recent_events` is reserved for the bridge.

### Manifest, version 1

`mikserstudio-plugin.json` holds:

- `id`, `name` and `version`;
- `apiVersion`, which restarts at 1;
- `entry`, a `.lua` file;
- `permissions`;
- `settings`, a JSON Schema;
- `contributes`: commands, menus, shortcuts and panels.

It has no `kind`: every plugin is a Lua plugin.

### Folders

- Built-in plugins: `plugins/builtin/<name>/`, staged to `bin/plugins/<name>/`.
- The example: `plugins/examples/hello/`, never installed.
- User plugins: `<data>/plugins/`, from `core::UserFolders`, one per instance. Plugin settings, which
  are kept in `<config>/plugins/` today, move to `<config>/plugin-settings/`, so the two folders never
  share a name.
- User plugins stay off until the person enables them and grants their permissions in the Plugins
  panel.

### Licensing

Open question for the user: under which licence third-party Lua plugins may be published, given
that they run inside a GPL-3.0-or-later program but only through its API.

## Conventions

- C++23, Qt 6.8+ (developed on 6.12). `QT_NO_KEYWORDS` is on: write `Q_SIGNALS`, `Q_SLOTS`, `Q_EMIT`.
  `QT_NO_CAST_FROM_ASCII` is on: wrap literals in `u"..."_s` (`using namespace Qt::StringLiterals`)
  or `QStringLiteral`.
- Naming: the product is **Mikser Studio** (only in text people read). Code uses `mikserstudio`
  (namespaces, plugin and panel ids), `MikserStudio::` for CMake targets and `MIKSERSTUDIO_` for
  macros and environment variables. Programs people run are
  `mikser-studio*`. Projects are `.msproj` files. The app id is `io.github.infinitinker.mikserstudio`
  (`kAppId` in `mikserstudio/core/Instance.h`); it must never change after a release.
- Namespaces follow folders: `mikserstudio::core`, `mikserstudio::engine`, `mikserstudio::ui`, ... Public headers live in
  `<module>/include/mikserstudio/<module>/`, private code in `<module>/src/`.
- Errors are explicit: return `std::expected<T, QString>` (or `CommandError`) with an English message
  that tells the user what went wrong and what to do. Never swallow errors; use `QLoggingCategory`
  (`mikserstudio.<module>`), not `qDebug` without a category.
- Small files with one job each. No dead or commented-out code, no speculative abstractions.
- All internal libraries are static; native plugins are shared libraries loaded through the C ABI
  until the Lua host replaces them.
- Tests use Catch2 v3 and run without a GPU, network or audio device (use MLT's `null` consumer and
  `color:` producers, fake transports, ephemeral ports).
