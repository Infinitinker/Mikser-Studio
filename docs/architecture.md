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
  isolation or heavy resources (AI models, export rendering, Blender, LibreOffice).
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
| `src/app/` | `mikserstudio` | all | Wires the modules together; `mikserstudio --headless` runs without UI. |
| `plugins/` | plugin libraries | sdk | Bundled example plugins. |
| `ai/` | Python package `mikserstudio-ai` | — | AI sidecar service, run from its own virtual environment (`ai/.venv`). |
| `tests/` | `*-tests` | Catch2 | Tests mirroring `src/`. |

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
  (`src/app/AppId.h`); it must never change after a release.
- Namespaces follow folders: `mikserstudio::core`, `mikserstudio::engine`, `mikserstudio::ui`, ... Public headers live in
  `<module>/include/mikserstudio/<module>/`, private code in `<module>/src/`.
- Errors are explicit: return `std::expected<T, QString>` (or `CommandError`) with an English message
  that tells the user what went wrong and what to do. Never swallow errors; use `QLoggingCategory`
  (`mikserstudio.<module>`), not `qDebug` without a category.
- Small files with one job each. No dead or commented-out code, no speculative abstractions.
- All internal libraries are static; native plugins are shared libraries loaded through the C ABI.
- Tests use Catch2 v3 and run without a GPU, network or audio device (use MLT's `null` consumer and
  `color:` producers, fake transports, ephemeral ports).
