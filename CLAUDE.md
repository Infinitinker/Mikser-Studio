# Mikser Studio

Mikser Studio is an AI-backed multimedia studio: media is gathered in a media bin, cut on a timeline and
played in its monitors, and AI tools generate and change media in the same flow, from a local AI service.
The aim is a creative flow no studio has offered yet, in an interface that stays polished and simple and
can be arranged at will: every panel can be moved, docked and undocked, as in Adobe Premiere. The app is
built around plugins and modules; a new tool comes as a plugin.

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
- Its machine forms: `mikserstudio` for namespaces, plugin and panel ids and the Python package
  `mikserstudio_ai`; `MikserStudio::` for CMake targets; `MIKSERSTUDIO_` for macros and environment
  variables; `mikser-studio*` for the programs people run. Projects are `.mikser` files, and plugin
  manifests `mikserstudio-plugin.json`.
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

Commit scopes: `ai`, `api`, `app`, `core`, `engine`, `plugins`, `sdk`, `services`, `ui`.

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

Existing open-source projects are a source of know-how and of what is in use today; learn from them,
and check a project's license before taking anything from it:

- Video editing: [Kdenlive](https://github.com/KDE/kdenlive), [Shotcut](https://github.com/mltframework/shotcut),
  [Blender](https://github.com/blender/blender), [VLC](https://github.com/videolan/vlc)
- Audio: [LMMS](https://github.com/LMMS/lmms), [Audacity](https://github.com/audacity/audacity)
- AI: [ComfyUI](https://github.com/Comfy-Org/ComfyUI), [InvokeAI](https://github.com/invoke-ai/InvokeAI),
  [Ollama](https://github.com/ollama/ollama), [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness/tree/master)
- Images and graphics: [GIMP](https://github.com/GNOME/gimp), [Inkscape](https://gitlab.com/inkscape/inkscape)
- Documents: [Euro-Office DesktopEditors](https://github.com/Euro-Office/DesktopEditors),
  [LibreOffice](https://github.com/LibreOffice/core)
- Other: [Strata](https://github.com/Niko1221/Strata/releases), [UniExtract2](https://github.com/Bioruebe/UniExtract2)

[ReflektorTTS](https://github.com/Infinitinker/ReflektorTTS) and
[Mockup Director](https://github.com/Infinitinker/Mockup-Director) are made the same way, with well-tried
guidelines: read them for how things are done there, but never change them from here.

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
- Python 3.13 for the AI service in `ai/`, in a plain `venv` in `ai/.venv`, with Ruff, mypy (strict),
  pytest and import-linter.
- clang-format and clang-tidy, set up by `.clang-format` and `.clang-tidy`.
- Inno Setup 7 for the Windows installer.

Before committing, run every check, from Git Bash with `/c/msys64/ucrt64/bin` first on `PATH`
([docs/development.md](docs/development.md#checks)):

- The build and its tests: `cmake --preset ci && cmake --build --preset ci`, then
  `ctest --preset ci -j8`.
- clang-format: `git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror`.
- clang-tidy: `cmake --build --preset ci --target autogen`, then
  `git ls-files '*.cpp' | xargs -P 8 -n 1 clang-tidy -p build/ci --quiet`.
- The include directions: `ai/.venv/Scripts/python tools/check_includes.py`.
- In `ai/`: `.venv/Scripts/python -m ruff check . ../tools`, `.venv/Scripts/python -m ruff format --check . ../tools`,
  `.venv/Scripts/python -m mypy`, `.venv/Scripts/python -m mypy ../tools`,
  `.venv/Scripts/python -m pytest` and `.venv/Scripts/lint-imports`.
- After changing what a release ships: `python tools/notices.py --check` and `python tools/licenses.py`,
  with MSYS2's Python.
- The tests of the scripts in `tools/`: from `ai/`, `.venv/Scripts/python -m pytest -c pyproject.toml ../tools/tests`.
- After changes to start-up, shutdown, the local API or the AI service, the end-to-end tests:
  `ctest --preset e2e`. After changes to the monitor, the GPU test: `ctest --preset gpu`.

`main` is stable, work happens on `dev`, changes reach `main` through pull requests.
