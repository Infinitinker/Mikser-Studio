# Mikser Studio

Mikser Studio is a multimedia studio where a person and AI agents make things together. Media is
gathered in a media bin, cut on a timeline and played in its monitors, and agents work in the same
project through the same commands as the person. A generator that turns one prompt into one finished
video leaves nothing to change; in Mikser Studio, what AI makes arrives as an editable project: every
shot, stem, title and effect stays a piece of its own that the person and the agents can change,
replace or generate again. The interface stays polished and simple and can be arranged at will: every
panel can be moved, docked and undocked, as in Adobe Premiere. The app is built around plugins and
modules; a new tool comes as a plugin.

The developer docs are in `docs/`: [architecture.md](docs/architecture.md) (the design, the modules,
import directions, the terms and the conventions), [development.md](docs/development.md) (setup,
running from source, the checks and the end-to-end tests), [releasing.md](docs/releasing.md),
[plugins.md](docs/plugins.md), [ai.md](docs/ai.md), [api.md](docs/api.md) and
[interface-texts.md](docs/interface-texts.md). Keep them current when the code changes.

Code written before these rules is being brought in line with them, stage by stage. Follow the rules in
new and changed code, and don't extend what they replace.

## Language and naming

- Everything is written in English, with American spelling (color, canceled, behavior, license):
  code, comments, docs, commit messages, CI and the interface. Interface texts follow
  [docs/interface-texts.md](docs/interface-texts.md).
- The product is **Mikser Studio**, in prose and the interface; it was once code-named Nexus, a name
  that is no longer used. The repository is `Infinitinker/Mikser-Studio`.
- Its machine forms: `mikserstudio` for namespaces and plugin and panel ids; `MikserStudio::` for
  CMake targets; `MIKSERSTUDIO_` for macros and environment variables; `mikser-studio*` for the
  programs people run. Projects are `.msproj` files, and plugin manifests `mikserstudio-plugin.json`.
- Files and folders take hyphens, never underscores: lowercase (`interface-texts.md`,
  `mikserstudio-plugin.json`), and uppercase for the documents at the root
  (`THIRD-PARTY-NOTICES.md`). Code files follow their language: C++ files in PascalCase, after the
  class or the job they hold (`KddwDockHost.cpp`, `Logging.h`), with `main.cpp` for a program; QML
  components and CMake modules in PascalCase (`Timeline.qml`, `MikserStudioCompilerOptions.cmake`);
  C and Python in snake_case (`plugin_api.h`, `check_includes.py`, `build_release.py`). Names a
  language, tool or GitHub fixes keep their form (`__init__.py`, `CMakeLists.txt`, and
  `.github/ISSUE_TEMPLATE` with its forms).
- The app id is `io.github.infinitinker.mikserstudio` (`kAppId` in
  `src/core/include/mikserstudio/core/Instance.h`); it never changes after a release. A build from
  source runs as **Mikser Studio Dev** (`io.github.infinitinker.mikserstudio.dev`), the end-to-end
  tests run **Mikser Studio E2E** (`io.github.infinitinker.mikserstudio.e2e`), and only the release,
  built with the `package` preset, runs as **Mikser Studio**.
- Per-user folders come from `core::UserFolders`, one set per instance and no organization folder: on
  Windows `%LOCALAPPDATA%\Mikser Studio` (or `… Dev`, `… E2E`) holds the settings, the API's
  `api.json` and `logs\mikser-studio.log`, with the cache in its `cache` folder.
- Release files are `MikserStudio-<version>-windows-x64-setup.exe`,
  `MikserStudio-<version>-windows-x64.zip` and `MikserStudio-<version>-third-party-notices.md`.
- Commits follow Conventional Commits, such as `fix(engine): keep the monitor in sync`; `build`, `ci`
  and `docs` take no scope.

Commit scopes: `ai`, `api`, `app`, `core`, `engine`, `mcp`, `plugins`, `sdk`, `services`, `ui`.

## Open source

Mikser Studio is under the GNU General Public License, version 3 or any later version
(GPL-3.0-or-later; `LICENSE`). A release ships FFmpeg, Qt HttpServer and KDDockWidgets under the GPL,
so it is under the GPL-3.0 as a whole ([docs/releasing.md](docs/releasing.md#licenses)).

What it uses or ships must be under a license that can be combined with the GPL-3.0-or-later.
`ALLOWED` in `tools/licenses.py` lists them, and the `Licenses` check enforces them:

- permissive: 0BSD, Apache-2.0, BSD-2-Clause, BSD-3-Clause, BSD-3-Clause-Clear, BSL-1.0, CC0-1.0,
  FTL, ICU, ISC, MIT, WTFPL and Zlib;
- copyleft: GPL-2.0-or-later, GPL-3.0-only, GPL-3.0-or-later, LGPL-2.0-or-later, LGPL-2.1-or-later,
  LGPL-3.0-only, LGPL-3.0-or-later and MPL-2.0;
- for manuals and images only: CC-BY-4.0, GFDL-1.3-no-invariants-only and GFDL-1.3-or-later.

A package whose metadata states its license otherwise is checked by hand and listed in `EXCEPTIONS`,
with the reason. GPL-2.0-only code, and licenses such as MPL-1.1 or AFL-2.1, can be used only where
another license is offered next to them.

## Learning from other projects

Other open-source projects show how things are done and what people use today. Read each one for what
it is listed for. Don't build their engines into Mikser Studio, and don't copy their code unless the
user decides to, after checking that its license can be combined with ours.

- How video editing, effects, 3D animation and an editor's layout work:
  [Kdenlive](https://github.com/KDE/kdenlive), [Shotcut](https://github.com/mltframework/shotcut),
  [Blender](https://github.com/blender/blender), [VLC](https://github.com/videolan/vlc).
- Advanced audio editing, filters and plugins such as VST: [LMMS](https://github.com/LMMS/lmms),
  [Audacity](https://github.com/audacity/audacity).
- How offline AI tools work, to plan how people and agents work together. Each has its own API,
  offline or online; Mikser Studio connects to them and never rebuilds them:
  [ComfyUI](https://github.com/Comfy-Org/ComfyUI), [InvokeAI](https://github.com/invoke-ai/InvokeAI),
  [Ollama](https://github.com/ollama/ollama), [Strata](https://github.com/Niko1221/Strata).
- How to talk to agents from inside the app, and let them act in it:
  [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness/tree/master).
- Image editing, raster and vector graphics, filters and effects: [GIMP](https://github.com/GNOME/gimp),
  [Inkscape](https://gitlab.com/inkscape/inkscape).
- Presentations and their animations, diagrams and fonts:
  [Euro-Office DesktopEditors](https://github.com/Euro-Office/DesktopEditors) (AGPL-3.0),
  [LibreOffice](https://github.com/LibreOffice/core).
- Opening many kinds of files: [UniExtract2](https://github.com/Bioruebe/UniExtract2).

## Principles

### Clean code

Good code standards hold, however fast the work goes: small modules with one job each, explicit errors
that tell the user what happened and what to do, logging through the `mikserstudio.<module>`
categories, no dead or commented-out code and no speculative abstractions. Each concept has one name,
listed in the terms table in [docs/architecture.md](docs/architecture.md#terms).

### Modularity

Modularity first: the app is built around plugins and modules. The core is headless, and everything the
user does is a command on its command bus, so menus, shortcuts, the command palette, plugins, macros and
the local API all work alike. Modules include only in the directions
[docs/architecture.md](docs/architecture.md#import-directions) allows; plugins see only the SDK. New
tools come as plugins, without changes to the core or the interface.

### Made together with agents

Agents are partners in the work, not a button that returns a finished file. These rules describe
where the app is going; new code moves toward them and never works against them.

- An agent acts only through the command bus, as the person does. Every change it makes shows in the
  project and can be undone. Where the app is going: each change says who made it (the person or
  which agent), and changes that belong together are one undo step with a name.
- Commands that delete or overwrite the person's work ask first, as other desktop apps do. Saving to
  the open project's own file never asks; saving over another existing file asks before replacing it;
  New and Open ask only when there are unsaved changes. A command that always replaces work carries a
  `confirmation`; one that does so only sometimes names a `confirmationParam` and answers
  `409 needs_confirmation` until the person agrees. The agent's client (the MCP bridge or the
  assistant in the app) shows the question and runs the command again only after the person agrees.
  Only a question the client puts to the person (MCP elicitation) counts as their agreement; a
  client's general permission prompt for tool calls does not.
- An agent sees the project through read-only commands: today `project.list` (media, tracks and
  clips). As the app grows: effects, keyframes and markers with stable ids, frames and contact sheets
  it can look at, audio analysis (loudness, silences, beats), and a transcript with word timings from
  a speech backend.
- The person guides the agents in the project itself. Where the app is going: a note on the timeline
  asks for a change at that spot, and a locked clip or track is left alone.
- Agents learn of the person's changes as they happen, through the API's events, and never overwrite
  them unseen.
- Agents outside the app, such as Claude Code, connect through the MCP bridge, a separate program that
  uses the local API; an assistant inside the app uses the same commands. The bridge and the assistant
  listen only on this computer. What leaves the computer is what an online service or an outside
  agent the person connects reads, and the interface says when one is connected.

### AI: connect, don't host

Mikser Studio runs no AI models itself and ships none. It connects to the AI tools the user runs,
local and offline first (Ollama, ComfyUI, InvokeAI, Strata), through their own APIs: one backend in
the AI engine per API. An online service is used only when the user sets it up. The AI engine never
downloads models or installs extensions unless the user asks.

What a backend makes arrives in the project as media of its own, with its provenance (the job,
backend, operation, model, parameters such as the prompt and seed, the time and the tool versions),
so it can be changed, replaced or generated again without touching the work around it.

### Scope

Video and sound on the timeline come first, with agents working in them. Images and graphics,
presentations and documents, 3D and more kinds of files come later, each as a module or plugin, when
the user decides to start them. Kinds of files will come from libraries such as FFmpeg and
libarchive, behind plugins that import them, not from parsers of our own.

### Portability

Windows is the first system, and the code stays portable to Linux and macOS: Qt, QRhi,
KDDockWidgets, MLT and RtAudio work on all three. Platform-specific code lives only in its own
module, behind a small interface. Per-user folders come from `core::UserFolders`, other paths from
Qt, never built by hand.

### Ease of use

Creativity comes first: a new user gets from download to a first result without reading docs or using
a terminal, through sensible defaults. The interface stays polished and simple, with customization in
mind: every panel can be moved, docked and undocked, and layouts are saved as workspaces.

## Tooling

- C++23 with Qt 6.8 or newer (developed on 6.12), KDDockWidgets, MLT 7 with FFmpeg and RtAudio, and
  Catch2, built with CMake presets (`debug`, `release`, `ci`, and `package` for the release files) and
  Ninja in MSYS2's UCRT64 environment.
- Our own code builds with free compilers only: MSYS2's GCC or Clang on Windows, GCC or Clang on
  Linux, Apple's toolchain on macOS. Build files, CI and docs offer no MSVC or Visual Studio route,
  not even as an alternative. Prebuilt dependencies and tools, such as python.org's Python and its
  wheels, may come from any compiler, with the runtime they need.
- Python 3.13 for the development scripts in `tools/`, which never ship, with their checks (Ruff, mypy
  (strict) and pytest, set up by `tools/pyproject.toml`) in a plain `venv` in `tools/.venv`.
- clang-format and clang-tidy, set up by `.clang-format` and `.clang-tidy`.
- Inno Setup 7 for the Windows installer.

Before committing, run every check, from Git Bash with `/c/msys64/ucrt64/bin` first on `PATH`
([docs/development.md](docs/development.md#checks)):

- The build and its tests: `cmake --preset ci && cmake --build --preset ci`, then
  `ctest --preset ci -j8`.
- clang-format: `git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror`.
- clang-tidy: `cmake --build --preset ci --target autogen`, then
  `git ls-files '*.cpp' | xargs -P 8 -n 1 clang-tidy -p build/ci --quiet`.
- The include directions: `tools/.venv/Scripts/python tools/check_includes.py`.
- In `tools/`: `.venv/Scripts/python -m ruff check .`, `.venv/Scripts/python -m ruff format --check .`,
  `.venv/Scripts/python -m mypy` and `.venv/Scripts/python -m pytest`.
- After changing what a release ships: `python tools/notices.py --check` and `python tools/licenses.py`,
  with MSYS2's Python.
- After changes to start-up, shutdown, the local API or the AI engine, the end-to-end tests:
  `ctest --preset e2e`. After changes to the monitor, the GPU test: `ctest --preset gpu`.

`main` is stable, work happens on `dev`, changes reach `main` through pull requests.
