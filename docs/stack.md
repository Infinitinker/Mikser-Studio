# Stack

What Mikser Studio is built with, and why. Each part was decided before the code it governs, with one reason. A change to this page is decided the same way, before any code depends on it.

Mikser Studio is a multimedia studio that the user shares with AI agents. Video comes first; sound and music editing grow to the level of a music studio such as LMMS. It works offline first: the connectors to AI services are optional. The first version imports, cuts and exports video, with its sound, and an agent can do all of it through the local API.

The proof of concept that came before taught most of what is here. Its base held up: C++, Qt, MLT, and one bus of commands for the interface, the API and agents. The stack choices that cost the most were a docking library that needed a workaround for almost everything, three ways of drawing in one window, and moving the editor's own features into Lua. The way decisions were made cost even more, which is why each part here is decided before its code.

## The parts

### Foundations

| Part | Choice | Why |
|---|---|---|
| Language | C++, to the C++23 standard | The media and audio libraries are C and C++, and C++23's `std::expected` gives one way to report errors. |
| Build | CMake with presets, and Ninja | What Qt, MLT and most C++ libraries use. One way to configure and build on every system, with the same presets in CI. |
| Framework | Qt 6, public modules only | One toolkit for the core's data types, the HTTP server and the interface, packaged on both systems. A private Qt module comes in only with a measured need: it ties the build to one Qt version. |
| Core | A library on Qt Core alone: commands with JSON Schema, events with sequence numbers, jobs, undo, the project file | The commands are the interface for the user and for agents at once. The core runs without a window, so tests, the API, headless runs and the render process use it as it is. |
| Project file | JSON with a format version; rational time, opaque ids, media paths relative to the project. The model has room from the start for every planned part: effects as data, audio and MIDI tracks, instrument and plugin slots, a color space for each media file. | Rational time keeps frame rates exact, and relative paths let a project move. The model stays close to OpenTimelineIO's, so cuts, clips, markers and time round-trip through it. The room for later parts is written in `docs/architecture.md` before the code, so adding them never reshapes the file. |

### Working with agents

| Part | Choice | Why |
|---|---|---|
| One owner of a project | The running app, with a window or headless, is the only process that edits a project, and holds a lock on it. Every command, from the user or an agent, runs through its one queue and lands in one undo history, where an agent's steps are labeled. Each side gets an event for every change. An agent's command names the revision it saw, and the app refuses it if what it touches has changed since. | The user and agents work on the same project at once: the user watches an agent's edits happen and can undo them, and the agent always knows what the user changed. One queue orders everything, so nothing has to be merged and no edit is lost unseen. |
| Agent interface | A local HTTP API on Qt HttpServer, on loopback with a session token. MCP is a mode of the same program, `mikser-studio --mcp`, over stdio: it passes commands to the running app, and starts the app if none is running. | Scripts and tests call the API directly; agents that speak MCP need only the app's path in their settings, with no second program to install. MCP stays a layer over the API, so a change in MCP never touches the core. The app itself enforces the user's agreement to destructive commands, for every caller that is not the user. |
| Connectors | Network job kinds in C++ (HTTP requests, streamed answers); connectors in Lua on top of them; commands declare the capability they provide | One connector per shared, open API: the OpenAI-compatible API first, then ComfyUI with the user's own workflow files. Callers ask for a capability (chat, transcription, images), not a connector. A connector is added without a new build of the app. |
| Keys for AI services | The system's keychain through QtKeychain; an environment variable, such as `OPENAI_API_KEY`, overrides it. A key is never written to a file. | Safe by default, as in editors such as Zed and VS Code. Where no keychain exists, such as on a headless Linux server, keys come from environment variables only. |

### Plugins

| Part | Choice | Why |
|---|---|---|
| Plugin language | Lua 5.4, through Lua's plain C API, for plugins and connectors. The editor's own features are C++. | Small and embeddable, and the app can sandbox it. The C API needs no extra library, is packaged everywhere, and lets Lua code wait on a job in a coroutine. |
| Plugin host | Each plugin runs on a thread of its own, in a sandbox: no `io`, `os`, `package`, `debug` or bytecode loading, and only the permissions its manifest declares. It reaches the app only through commands, events and jobs. | A plugin can't freeze the interface or reach what it wasn't granted. Values cross as copies, which suits plugins; the editor's own features never pay that cost, since they are C++. |

### Media and sound

| Part | Choice | Why |
|---|---|---|
| Media engine | MLT 7 on FFmpeg for video, used only inside the engine module | The engine of Kdenlive and Shotcut: decoding, playback, effects, transitions and encoding without building our own. Effects and transitions are MLT services that the timeline attaches, so they reach the export. |
| Audio engine | An engine of our own, on a real-time thread, which plays all sound from the first version: MLT decodes the clips' audio, and the engine mixes it and plays it in step with the video. The mixer, effect chains, automation, instruments and MIDI (a piano roll, patterns) build on it. It uses PortAudio for the sound card, RtMidi for MIDI, libsndfile and libsamplerate for audio files and rates, FluidSynth for SoundFonts and libebur128 for loudness. | Music editing at LMMS's level needs an engine that MLT doesn't have, and playing sound through MLT first would mean rebuilding the playback sync later. LMMS's own engine is part of its app, not a library, so it is a reference: its code is GPL-2.0-or-later, and single pieces can be adapted. PortAudio is what Audacity and LMMS use, with WASAPI and ASIO on Windows and ALSA and JACK on Linux. |
| Audio plugins | CLAP and LV2 (through lilv) and the app's own instruments and effects run in the app's process. VST3 plugins run in a separate host process. | The open formats and our own instruments get the fast path. VST3 plugins are the most varied and the most likely to crash, so, as in LMMS, they can't take the app down. |
| Export | A separate render process: the app itself, started headless, renders a saved snapshot of the project, and renders can queue. Playback and decoding while editing run in the app. | A long render doesn't slow editing, and a crash while rendering can't lose the open project. The render runs the same engines, so it gives the same result at the same speed; MLT's `melt` alone can't, since it doesn't have our audio engine. The proof of concept's rules carry over: render a fixed snapshot, refuse missing media, replace the file in one step. |
| Open standards | Each standard's room in the project file and the core's interfaces from the start; its code when its work starts. OpenTimelineIO first; then OpenColorIO, OpenFX, Lottie, CLAP, LV2 and VST3. | No API is guessed and no code waits unused, and adding a standard never reshapes the foundation. |

### Interface

| Part | Choice | Why |
|---|---|---|
| Interface | Qt Widgets for the window, menus, dialogs and docking; Qt Quick for the timeline and for forms drawn from schemas; one theme; accessible in both | The arrangement Kdenlive and Shotcut use. Not a full Qt Quick shell, whose menus, dialogs and docking are weaker, and not Widgets alone, which would paint the timeline by hand. The window is built hidden and shown once; before the interface is built on it, a one-day spike on a real screen confirms there is no blink. |
| Docking | Qt's own `QMainWindow` and `QDockWidget`, behind an interface of the app's own; the workspace file is the app's own format | No extra library and no license question. Another docking library can come in behind the interface if a named need is not met. |
| Monitor | The simplest surface that plays without tearing, drawn inside the window | Docking, overlays and screenshots work as everywhere else. A native GPU window comes only if a measurement shows dropped frames: nothing can be drawn over it, and it complicates docking. |

### Systems and quality

| Part | Choice | Why |
|---|---|---|
| Systems | Windows and Ubuntu 26.04, both built and tested in CI from the first commit, with sanitizers on Linux | Linux from day one keeps "Linux later" a packaging task, not a port. |
| Windows toolchain | MSYS2 UCRT64 with GCC, pinned to a dated snapshot | One GCC on both systems, and MLT, Qt, FFmpeg and the audio libraries prebuilt from one package set. MSVC with vcpkg or KDE Craft is not used: the project builds with free compilers only. The pin is moved on purpose, in one pull request, so CI can't break with no change of ours. |
| Packaging | A Windows installer (Inno Setup) built in CI from the pinned MSYS2 files; the Linux format decided before the first release; license notices generated at release time | Built before the first release, not before: until then there is nothing to install. The first release needs a lawful package on both systems. |
| Tests | Catch2 v3, one test program per module, with labels for slow and end-to-end runs; Qt Test's helpers only to simulate input in interface tests; Lua plugins tested by a spec runner in the app's own plugin host | Plain, readable C++ tests, and plugins tested where they run. |
| Checks | C++: clang-format, clang-tidy, a check of which module may include which. Lua: StyLua, luacheck, `luac -p`. Python: ruff and mypy. Spelling, the terms list, and a license check of the dependencies. All in CI from the start. | Cheapest while the code is new: a check added later finds a backlog. The proof of concept's layering held because a script checked it. |
| Tools | Python 3 for the repository's own scripts | Its standard library covers files, processes, options and JSON. Plain Lua has none of these, and the proof of concept's Lua tools needed helper modules that rebuilt them. |
| License | GPL-3.0-or-later for the app's own code | MLT's GPL modules, FFmpeg's GPL encoders and Qt HttpServer point there, and LMMS's code (GPL-2.0-or-later) can be adapted into it. Qt HttpServer is GPL-3.0-only, so a released build as a whole is under GPL-3.0. |

## Versions

The lower version of the two systems is the minimum; MSYS2's newer versions are pinned with its snapshot.

| Dependency | Ubuntu 26.04 | MSYS2 UCRT64 (database of 2026-11-12) | Minimum |
|---|---|---|---|
| GCC | 15.2 | 16.2 | 15 |
| Clang (clang-format, clang-tidy) | 21.1 | 23.1 | 21 |
| CMake | 4.2.3 | 4.4.5 | 4.2 |
| Ninja | 1.13.2 | 1.13.2 | 1.13 |
| Qt 6 (Base, Declarative, HttpServer) | 6.10.2 | 6.12.0 | 6.10 |
| MLT | 7.36.1 | 7.42.0 | 7.36 |
| FFmpeg | 8.0.1 | 9.0.2 | 8.0 |
| PortAudio | 19.7.0 | 19.7.0 | 19.7 |
| Lua | 5.4.8 (`liblua5.4-dev`) | 5.4.9 (`lua54`; plain `lua` is 5.5) | 5.4 |
| Catch2 | 3.7.1 | 3.16.1 | 3.7 |
| libebur128 | 1.2.6 | 1.2.6 | 1.2 |

When their work starts:

| Dependency | Ubuntu 26.04 | MSYS2 UCRT64 |
|---|---|---|
| QtKeychain | 0.15.0 | 0.18.0 |
| RtMidi | 6.0.0 | 6.0.0 |
| libsndfile | 1.2.2 | 1.2.2 |
| libsamplerate | 0.2.2 | 0.2.2 |
| FluidSynth | 2.4.8 | 2.6.2 |
| lilv (LV2) | 0.26.2 | 0.28.0 |
| OpenTimelineIO | 0.18.1 | 0.18.1 |
| OpenColorIO | 2.5.1 | 2.6.0 |

Neither system packages the CLAP headers, and only Ubuntu packages the VST3 SDK (3.7.10): both come in at a pinned release when their work starts. Python and the Lua checkers get their rows when the tools that use them are added.

## Not used, and why

- **KDDockWidgets:** it needed a workaround for almost everything it was chosen for, several of which rewrite its private layout file; the interface meant to hide it leaked; it keeps global state; and its license is GPL or commercial.
- **sol2:** used in one file for what the C API does. Ubuntu 26.04 does not package it, and a Lua coroutine cannot yield across a call through it.
- **The editor's features in Lua:** every call crossed two threads and was copied twice. That suits sandboxed plugins, not the editor's core.
- **QML panels from plugins:** QML from a plugin runs with the app's rights, so it is no sandbox. When plugins get panels, they describe them as data, which the app draws.
- **Merging copies of a project:** two processes editing copies, merged later (as with a CRDT), would make timelines conflict in ways no merge resolves well. One owner runs every edit in order instead.
- **A separate MCP bridge:** a second program to package and keep in step, with nothing it could do that the app's own MCP mode can't.
- **Keys in a plain file:** anyone who can read the file has the key.
- **MLT's audio output, and Qt Multimedia's:** neither is built for low latency, ASIO or JACK, which music editing needs.
- **`melt` as the render process:** it doesn't run our audio engine.
- **Every standard built early:** code with no feature using it gets its API guessed and reworked; the room for each standard is made early instead.
- **An effect system of our own on the GPU:** MLT already has effects and transitions as services the timeline attaches, and effects drawn after MLT never reach the export. A GPU path waits for a measurement.
- **Hardware decoding, a GPU compositor of our own and a native GPU monitor:** not before a measurement asks for them.
- **Lua for the repository's tools:** see Tools.
- **An installer before the first release:** a slow job to keep green with nothing to install.
