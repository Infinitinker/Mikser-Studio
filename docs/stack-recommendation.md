# Mikser Studio: recommended cross-platform stack

## Recommendation

**Build Mikser Studio as a native C++23 app on Qt 6.12, with a headless core and an AI engine that connects to the AI tools the user runs.** It starts from the qt-native proposal, which scored highest (24.1/30), and adds the best plugin and isolation ideas from python-first (21.7).

All the multiplatform media apps you listed as references ship on this foundation: Kdenlive, Shotcut, Krita, OBS and Audacity 4. Qt draws through the native graphics API on each OS (D3D11/12 on Windows, Metal on macOS, Vulkan or GL on Linux). That avoids differences between the three system webviews. It also gives you mature Wayland, HiDPI, multi-monitor, accessibility and input-method support.

## Stack by layer

| Layer | Choice |
|---|---|
| Toolchain | C++23, CMake presets and Ninja. Free compilers only: MSYS2's GCC or Clang in its UCRT64 environment on Windows, with the dependencies from MSYS2's packages; GCC or Clang on Linux; Apple's toolchain on macOS. clang-tidy and ASan/TSan/UBSan required in CI. The development scripts in `tools/` are Python, checked with Ruff, mypy (strict) and pytest; they never ship. |
| UI | Qt 6.12. Qt Widgets for the shell, menus and property forms. QML for the timeline, node graph and AI panels (the same split Kdenlive uses). One set of design tokens shared by the QSS and QML themes. |
| Docking | KDDockWidgets 2.5.x (GPL or commercial; Widgets and QML frontends; can save and restore individual windows). It sits behind a small `mikserstudio::ui::DockHost` interface. Qt Advanced Docking System 5.1.1 (LGPL-2.1, full Wayland support since 5.0) is the drop-in fallback. |
| GPU views | QRhi. The **program and source monitors use a QRhi-backed QWindow inside `createWindowContainer`**, not QRhiWidget. QRhiWidget draws into an extra texture first, which costs performance. QRhiWidget and QML are kept for secondary panels. OpenColorIO 2.x handles color, and scopes run as compute shaders on the full-resolution frame. |
| Video engine | MLT 7.42 on FFmpeg 9, with hardware decoding (D3D11VA/D3D12VA, VideoToolbox, VAAPI/Vulkan). A custom MLT consumer feeds a lock-free ring buffer. OpenTimelineIO handles project interchange. The libplacebo module only covers scaling and tonemapping for now. A **GPU compositor sits behind an engine interface from day one** so MLT's CPU compositing can be replaced step by step. |
| Audio engine | Our own C++ real-time graph. Device I/O through RtAudio 6 (WASAPI/ASIO, CoreAudio, PipeWire/JACK/ALSA). Hosts CLAP, VST3 SDK 3.8 (MIT) and LV2 via lilv. MIDI through libremidi. **The audio device clock is the master clock** for all playback. |
| Core / API | `mikserstudio-core` is a C++ library with a command bus: every action is a typed, undoable command described in JSON Schema. That one registry feeds undo, the command palette, macros, shortcuts, the /v1 HTTP+WebSocket API on 127.0.0.1 (with a session token) and an MCP bridge, a separate program that offers the commands to agents such as Claude Code through that API. `mikser-studio --headless` runs the core with no UI. |
| Scripting | No embedded interpreter: macros are commands on the command bus, and scripts in any language drive the app through the /v1 API. |
| AI | Mikser Studio runs no AI models itself and ships none. The AI engine, in the app's C++ services module, connects to the AI tools the user runs, local and offline first, through their own APIs, one backend per API: Ollama, ComfyUI, later InvokeAI and Strata. An online service is used only when the user sets it up. |
| Other domains | Later, each as a module or plugin, when the user decides to start it: images and graphics, presentations and documents, 3D. More kinds of files come from libraries such as FFmpeg and libarchive, behind plugins that import them, not from parsers of our own. |
| Packaging | GitHub Actions. Windows builds run in MSYS2's UCRT64 environment, whose packages setup-msys2 caches, with ccache through the CMake presets and pip's download cache for the checks of the scripts in `tools/`; macOS arm64 and Ubuntu builds come later. Windows: `tools/build_release.py` copies the program with the MSYS2 libraries it imports, MLT's modules and Qt's plugins and QML modules, then makes a zip and an Inno Setup 7 installer. macOS: macdeployqt, notarization, DMG. Linux: Flatpak on the KDE runtime, plus AppImage. A release ships no Python. The AI engine downloads no models: it uses the AI tools the user has installed. |

## Process model

- **Main process:** UI, core, MLT playback, audio graph, the AI engine.
- **`mikserstudio-render`:** export runs in its own process, as Kdenlive's render process and Shotcut's melt jobs do. A crash or long export cannot stall editing.
- **Plugin scanner:** CLAP/VST3/LV2/OFX scanning runs out of process, and plugins that crash are blocklisted. Out-of-process audio plugin hosting is optional.
- **AI engine:** in the main process, it calls the AI tools the user runs (Ollama, ComfyUI) through their APIs, with asynchronous requests. Those tools are programs of their own and manage their own GPU memory; the engine runs one job at a time per backend.
- **MCP bridge:** a separate program, started by the agent's client, that offers the commands to agents outside the app through the local API.
- **Generated media** arrives in the project as media of its own, with its provenance (the job, backend, operation, model, parameters such as the prompt and seed, the time and the tool versions), so it can be changed, replaced or generated again.

A module is not a process: separate processes only for isolation or heavy resources.

## Plugin model

Each plugin has a `mikserstudio-plugin.json` manifest: id, SemVer version, supported API range, capabilities and a settings JSON Schema. Plugins are discovered without loading their code and activated lazily.

1. **Native C/C++ plugins:** stable C ABI (`sdk/include/mikserstudio/plugin_api.h`, the same approach as OBS's libobs), loaded with QPluginLoader.
2. **Standard formats:** OFX, frei0r, CLAP/VST3/LV2, OCIO configs, ComfyUI workflows and nodes.

How plugins contribute UI:
- Settings forms are generated automatically from the JSON Schema.
- Plugins that run as programs of their own can ship QML panels. The host binds them to a proxy object that forwards to the plugin's /v1 endpoints, so plugin UI does not depend on the Qt binary version.
- Every panel is registered as `<plugin-id>/<panel-id>`. A missing plugin's panel becomes a placeholder, so saved workspaces survive plugin removal.

The public contract ships as a separate, versioned `mikserstudio-sdk` with CI checks for breaking changes. CI also runs the UI test suite against an out-of-process core to prove the UI never bypasses the API.

Possible later addition: WASM components (wasmtime) as a sandboxed tier for non-real-time effects, importers and analyzers.

## Licensing

- **App:** GPL-3.0-or-later. KDDockWidgets, QtHttpServer, GPL FFmpeg encoders, the ASIO SDK (now GPLv3) and borrowed Kdenlive/Shotcut code all point there anyway.
- **SDK:** the C header and manifest schema under MIT/Apache-2.0.
- **Plugin exception:** you can add one for your own code, but it cannot cover third-party GPL parts like KDDockWidgets. Closed-source plugins should therefore talk to Mikser Studio over /v1 or run as sidecars.
- **License gating in CI:** `tools/licenses.py` checks the licenses of the MSYS2 packages a release ships against an allow list, and `tools/notices.py` writes their notices, so no AGPL or GPL-only dependency slips in unnoticed.
- **FFmpeg build switch:** choose per channel between LGPL FFmpeg with OS/hardware encoders and a GPL build with x264/x265. Codec patents are a separate distribution question.
- **Model weights:** Mikser Studio ships none; where it lists the models an AI tool offers, it shows each model's license when the tool reports it.
- **Permissive escape hatch if ever needed:** Qt ADS instead of KDDockWidgets, cpp-httplib instead of QtHttpServer, LGPL FFmpeg.

## Top risks and mitigations

| Risk | Mitigation |
|---|---|
| C++ development speed and memory safety | Sanitizers and fuzzing are mandatory. Strict review of real-time code. |
| MLT composites on the CPU, so the performance ceiling is roughly Kdenlive/Shotcut | Proxies now. GPU compositor behind the engine interface, rolled out progressively. |
| Zero-copy hardware decode into QRhi is hand-written per OS (D3D11 / IOSurface-Metal / DMA-BUF-Vulkan) | Create FFmpeg's hardware device on the same GPU device QRhi uses. Keep a CPU-upload fallback at all times. |
| Two engines (MLT and the audio graph) staying in sync | Audio clock is the master as a hard rule. A/V drift tests in CI. |
| Docking on Wayland and macOS: Wayland apps cannot place their own windows, and macOS Spaces have quirks | Accept approximate position restore on Wayland and offer XWayland as an opt-in. Test docking on every OS for every release. |
| QRhi can change between Qt minor releases. Qt's 5-year LTS patches go to commercial licensees only | Track Qt minor releases twice a year, or budget for a commercial license. |
| AI tools and GPU memory | The AI tools are the user's own programs, reached through their APIs, so Mikser Studio pins and packages none of them. They share the GPU with playback: one job at a time per backend, and models unloaded when a job ends where the tool allows it. Custom nodes are treated as untrusted code. |
| Build complexity across three OSes | Prebuilt packages (MSYS2's on Windows), so no dependency is built from source, and one set of CMake presets. Shotcut's build scripts as the reference recipe. |
| Scope (an "everything studio") | Video and sound on the timeline come first, with agents working in them. Other kinds of work come later, each as a module or plugin, when the user decides to start it. |

## Build first: a thin vertical slice

The biggest unknown is whether a docked, floatable monitor gets native GPU playback on all three OSes, synced to audio, with AI output coming back into the timeline. The first slice should be exactly that:

1. Qt 6.12 + KDDockWidgets shell with three panels: a QRhi QWindow monitor, a QML timeline and an AI panel. Saving and restoring a workspace must work.
2. Import a clip, then play it through MLT → custom consumer → monitor using hardware decoding (zero-copy on at least one OS, CPU fallback elsewhere), with RtAudio as the master clock.
3. Float the monitor onto a second display on Windows, macOS, X11 and Wayland, and measure dropped frames at 1080p60 and with 4K proxies.
4. One command, `ai.upscale_clip`, sent over /v1: an AI backend makes the result, which comes back into the project with its provenance, is put on the timeline, and can be undone.
5. A three-OS CI build with installers produced.

Pass criteria: no visible A/V drift, under two frames of latency from scrub to screen, and the floating monitor survives a workspace restore on every OS.

## Why the other options lost

- **python-first (21.7):** Same Qt/MLT foundation, but the Python core shares its interpreter lock with the UI thread, so a busy plugin freezes the UI. It also needs three binding technologies (shiboken6, nanobind, PyO3), and shipping a relocatable interpreter makes macOS signing and installers harder. It also planned around an outdated Qt ADS 4.5. Its isolation ideas were adopted above.
- **rust-native (14.9):** No Rust toolkit can tear panels off into native windows on other monitors (egui_dock's multi-viewport work is still open), so the main docking requirement would be custom work on a toolkit that breaks its API every quarter. It also has no OCIO, OFX, OTIO or VST3 support, and plugins would need an invented UI protocol.
- **tauri-web (13.0):** A webview cannot show the engine's GPU output natively, so every docked monitor frame needs a GPU readback and a copy. The proposal itself admits 4K60 in-panel playback is not achievable on macOS/Linux, and WebKitGTK makes Linux the weakest platform. Two comparable projects moved off Tauri for this kind of work: Graphite (to CEF with a native viewport) and Cap (to a native GPUI app).

**Claims I discarded because the judges flagged them:**
- That the qt-native design gives the "best achievable" real-time performance.
- That zero-copy into QRhi is routine.
- That Kdenlive's KDDockWidgets proof of concept was built for a detachable timeline.
- That Krita AI Diffusion runs on Krita 6. Its documented requirement is Krita 5.2+.
- That open-source users get Qt's 5-year LTS patches.
