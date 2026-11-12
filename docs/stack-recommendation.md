# Mikser Studio: recommended cross-platform stack

## Recommendation

**Build Mikser Studio as a native C++20 app on Qt 6.12, with a headless core and Python AI sidecars.** It starts from the qt-native proposal, which scored highest (24.1/30), and adds the best plugin and isolation ideas from python-first (21.7).

All the multiplatform media apps you listed as references ship on this foundation: Kdenlive, Shotcut, Krita, OBS and Audacity 4. Qt draws through the native graphics API on each OS (D3D11/12 on Windows, Metal on macOS, Vulkan or GL on Linux). That avoids differences between the three system webviews. It also gives you mature Wayland, HiDPI, multi-monitor, accessibility and input-method support.

## Stack by layer

| Layer | Choice |
|---|---|
| Toolchain | C++23, CMake presets and Ninja. Free compilers only: MSYS2's GCC or Clang in its UCRT64 environment on Windows, with the dependencies from MSYS2's packages; GCC or Clang on Linux; Apple's toolchain on macOS. clang-tidy and ASan/TSan/UBSan required in CI. Python side uses ruff, mypy and pytest (the ReflektorTTS conventions). |
| UI | Qt 6.12. Qt Widgets for the shell, menus and property forms. QML for the timeline, node graph and AI panels (the same split Kdenlive uses). One set of design tokens shared by the QSS and QML themes. |
| Docking | KDDockWidgets 2.5.x (GPL or commercial; Widgets and QML frontends; can save and restore individual windows). It sits behind a small `mikserstudio::ui::DockHost` interface. Qt Advanced Docking System 5.1.1 (LGPL-2.1, full Wayland support since 5.0) is the drop-in fallback. |
| GPU views | QRhi. The **program and source monitors use a QRhi-backed QWindow inside `createWindowContainer`**, not QRhiWidget. QRhiWidget draws into an extra texture first, which costs performance. QRhiWidget and QML are kept for secondary panels. OpenColorIO 2.x handles color, and scopes run as compute shaders on the full-resolution frame. |
| Video engine | MLT 7.42 on FFmpeg 9, with hardware decoding (D3D11VA/D3D12VA, VideoToolbox, VAAPI/Vulkan). A custom MLT consumer feeds a lock-free ring buffer. OpenTimelineIO handles project interchange. The libplacebo module only covers scaling and tonemapping for now. A **GPU compositor sits behind an engine interface from day one** so MLT's CPU compositing can be replaced step by step. |
| Audio engine | Our own C++ real-time graph. Device I/O through RtAudio 6 (WASAPI/ASIO, CoreAudio, PipeWire/JACK/ALSA). Hosts CLAP, VST3 SDK 3.8 (MIT) and LV2 via lilv. MIDI through libremidi. **The audio device clock is the master clock** for all playback. |
| Core / API | `mikserstudio-core` is a C++ library with a command bus: every action is a typed, undoable command described in JSON Schema. That one registry feeds undo, the command palette, macros, shortcuts, the /v1 HTTP+WebSocket API on 127.0.0.1 (with a session token) and an MCP server. `mikserstudio --headless` runs the core with no UI. |
| Scripting | Embedded CPython 3.13 with nanobind, for trusted macros only. |
| AI | (1) Python 3.13 sidecar in its own virtual environment, running PyTorch (CUDA/ROCm/XPU/MPS chosen at first run) and a pinned headless ComfyUI. Frames move through shared memory via a `mikserstudio_bridge` custom-node pack. (2) Ollama on :11434. (3) whisper.cpp and ONNX Runtime run in-process for small, latency-critical models. |
| Other domains | Raster: own tiled QRhi canvas. Vector: lib2geom + resvg. 3D: Blender via bpy as a sidecar. Documents: LibreOfficeKit as a sidecar. Archives: libarchive plus a 7-Zip sidecar. |
| Packaging | GitHub Actions. Windows builds run in MSYS2's UCRT64 environment, whose packages setup-msys2 caches, with ccache through the CMake presets and pip's download cache for the Python side; macOS arm64 and Ubuntu builds come later. Windows: `tools/build_release.py` copies the program with the MSYS2 libraries it imports, MLT's modules and Qt's plugins and QML modules, then makes a zip and an Inno Setup 7 installer. macOS: macdeployqt, notarization, DMG. Linux: Flatpak on the KDE runtime, plus AppImage. AI components download on demand. |

## Process model

- **Main process:** UI, core, MLT playback, audio graph, embedded Python.
- **`mikserstudio-render`:** export runs in its own process, as Kdenlive's render process and Shotcut's melt jobs do. A crash or long export cannot stall editing.
- **Plugin scanner:** CLAP/VST3/LV2/OFX scanning runs out of process, and plugins that crash are blocklisted. Out-of-process audio plugin hosting is optional.
- **AI sidecar(s):** ComfyUI and the Mikser Studio AI service, plus Ollama.
- **Blender and LibreOffice sidecars.**
- **GPU/VRAM scheduler** in the core: real-time playback has priority, and AI jobs pause or scale down during playback.
- **Generated media** is cached to disk as proxies, keyed by a hash of the workflow plus its inputs, with full provenance (workflow, seed, model hash).

This follows the ReflektorTTS rule that a module is not a process: separate processes only for isolation or heavy resources.

## Plugin model

Each plugin has a `mikserstudio-plugin.toml` manifest: id, SemVer version, supported API range, capabilities and a settings JSON Schema. Plugins are discovered without loading their code and activated lazily.

1. **Native C/C++ plugins:** stable C ABI (`mikserstudio_plugin_api.h`, the same approach as OBS's libobs), loaded with QPluginLoader.
2. **Python plugins:** entry points behind Protocols, as in ReflektorTTS. Trusted plugins run in-process. Others run in their own virtual environment as a sidecar. A plugin whose dependencies conflict with the core's is moved to a sidecar automatically.
3. **Standard formats:** OFX, frei0r, CLAP/VST3/LV2, OCIO configs, ComfyUI workflows and nodes.

How plugins contribute UI:
- Settings forms are generated automatically from the JSON Schema.
- Sidecar plugins can ship QML panels. The host binds them to a proxy object that forwards to the plugin's /v1 endpoints, so plugin UI does not depend on the PySide or Qt binary version.
- Every panel is registered as `<plugin-id>/<panel-id>`. A missing plugin's panel becomes a placeholder, so saved workspaces survive plugin removal.

The public contract ships as a separate, versioned `mikserstudio-sdk` with CI checks for breaking changes. CI also runs the UI test suite against an out-of-process core to prove the UI never bypasses the API.

Possible later addition: WASM components (wasmtime) as a sandboxed tier for non-real-time effects, importers and analyzers.

## Licensing

- **App:** GPL-3.0-or-later, matching ReflektorTTS. KDDockWidgets, QtHttpServer, GPL FFmpeg encoders, the ASIO SDK (now GPLv3) and borrowed Kdenlive/Shotcut code all point there anyway.
- **SDK:** the C header, Protocol package and manifest schema under MIT/Apache-2.0.
- **Plugin exception:** you can add one for your own code, but it cannot cover third-party GPL parts like KDDockWidgets. Closed-source plugins should therefore talk to Mikser Studio over /v1 or run as sidecars.
- **License gating in CI:** `tools/licenses.py` checks the licenses of the MSYS2 packages a release ships against an allow list, and `tools/notices.py` writes their notices, so no AGPL or GPL-only dependency slips in unnoticed. The Python dependencies get the same check once the AI service ships.
- **FFmpeg build switch:** choose per channel between LGPL FFmpeg with OS/hardware encoders and a GPL build with x264/x265. Codec patents are a separate distribution question.
- **Model weights:** show each model's license in the model manager.
- **Permissive escape hatch if ever needed:** Qt ADS instead of KDDockWidgets, cpp-httplib instead of QtHttpServer, LGPL FFmpeg.

## Top risks and mitigations

| Risk | Mitigation |
|---|---|
| C++ development speed and memory safety | Sanitizers and fuzzing are mandatory. Python for orchestration and plugins. Strict review of real-time code. |
| MLT composites on the CPU, so the performance ceiling is roughly Kdenlive/Shotcut | Proxies now. GPU compositor behind the engine interface, rolled out progressively. |
| Zero-copy hardware decode into QRhi is hand-written per OS (D3D11 / IOSurface-Metal / DMA-BUF-Vulkan) | Create FFmpeg's hardware device on the same GPU device QRhi uses. Keep a CPU-upload fallback at all times. |
| Two engines (MLT and the audio graph) staying in sync | Audio clock is the master as a hard rule. A/V drift tests in CI. |
| Docking on Wayland and macOS: Wayland apps cannot place their own windows, and macOS Spaces have quirks | Accept approximate position restore on Wayland and offer XWayland as an opt-in. Test docking on every OS for every release. |
| QRhi can change between Qt minor releases. Qt's 5-year LTS patches go to commercial licensees only | Track Qt minor releases twice a year, or budget for a commercial license. |
| AI packaging and VRAM contention | A virtual environment per sidecar, ComfyUI pinned to a tag, VRAM scheduler, custom nodes treated as untrusted code. |
| Build complexity across three OSes | Prebuilt packages (MSYS2's on Windows), so no dependency is built from source, and one set of CMake presets. Shotcut's build scripts as the reference recipe. |
| Scope (an "everything studio") | Video, audio and AI form the core. 3D and documents start as sidecar bridges. |

## Build first: a thin vertical slice

The biggest unknown is whether a docked, floatable monitor gets native GPU playback on all three OSes, synced to audio, with AI output coming back into the timeline. The first slice should be exactly that:

1. Qt 6.12 + KDDockWidgets shell with three panels: a QRhi QWindow monitor, a QML timeline and an AI panel. Saving and restoring a workspace must work.
2. Import a clip, then play it through MLT → custom consumer → monitor using hardware decoding (zero-copy on at least one OS, CPU fallback elsewhere), with RtAudio as the master clock.
3. Float the monitor onto a second display on Windows, macOS, X11 and Wayland, and measure dropped frames at 1080p60 and with 4K proxies.
4. One command, `ai.upscale_clip`, sent over /v1: frames go to ComfyUI through shared memory, the result is cached as a proxy, put on the timeline, and can be undone.
5. A three-OS CI build with installers produced.

Pass criteria: no visible A/V drift, under two frames of latency from scrub to screen, and the floating monitor survives a workspace restore on every OS.

## Why the other options lost

- **python-first (21.7):** Same Qt/MLT foundation, but the Python core shares its interpreter lock with the UI thread, so a busy plugin freezes the UI. It also needs three binding technologies (shiboken6, nanobind, PyO3), and shipping a relocatable interpreter makes macOS signing and installers harder. It also planned around an outdated Qt ADS 4.5. Its plugin-ecosystem and isolation ideas were adopted above.
- **rust-native (14.9):** No Rust toolkit can tear panels off into native windows on other monitors (egui_dock's multi-viewport work is still open), so the main docking requirement would be custom work on a toolkit that breaks its API every quarter. It also has no OCIO, OFX, OTIO or VST3 support, and plugins would need an invented UI protocol.
- **tauri-web (13.0):** A webview cannot show the engine's GPU output natively, so every docked monitor frame needs a GPU readback and a copy. The proposal itself admits 4K60 in-panel playback is not achievable on macOS/Linux, and WebKitGTK makes Linux the weakest platform. Two comparable projects moved off Tauri for this kind of work: Graphite (to CEF with a native viewport) and Cap (to a native GPUI app).

**Claims I discarded because the judges flagged them:**
- That the qt-native design gives the "best achievable" real-time performance.
- That zero-copy into QRhi is routine.
- That Kdenlive's KDDockWidgets proof of concept was built for a detachable timeline.
- That Krita AI Diffusion runs on Krita 6. Its documented requirement is Krita 5.2+.
- That open-source users get Qt's 5-year LTS patches.
