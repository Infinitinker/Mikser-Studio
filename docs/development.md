# Development

How to set up Mikser Studio from source, run it, and run its checks and tests. How a release is built
is in [releasing.md](releasing.md), and how the code is organized in [architecture.md](architecture.md).

## Windows (MSYS2 UCRT64)

Mikser Studio builds on Windows with the MSYS2 **UCRT64** toolchain, the same way Shotcut does. Install
[MSYS2](https://www.msys2.org/), then the dependencies from an MSYS2 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,gdb,cmake,ninja,ccache,pkgconf} \
  mingw-w64-ucrt-x86_64-{qt6-base,qt6-declarative,qt6-shadertools,qt6-tools,qt6-httpserver,qt6-websockets,qt6-svg} \
  mingw-w64-ucrt-x86_64-{kddockwidgets,mlt,ffmpeg,rtaudio,catch} \
  mingw-w64-ucrt-x86_64-{libebur128,fftw,libsamplerate,sox,rubberband,rnnoise,lilv}
```

The last line is MLT's optional runtime libraries. Without them MLT silently skips modules: `qt6`
(the `qtblend` compositor), `plus`, `resample`, `sox`, `rubberband`, `rnnoise` and `ladspa`.

The C++ checks need clang-format and clang-tidy, and the license tools MSYS2's Python:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{clang,clang-tools-extra,python}
```

The AI service needs [Python 3.13](https://www.python.org/downloads/) and its own virtual environment
in `ai/.venv`, made with `venv` and `pip` once (and again after its dependencies change), from Git Bash
or PowerShell. An MSYS2 shell doesn't see the Windows `py` launcher, and MSYS2's own `python` makes a
venv the app does not look for.

```sh
py -3.13 -m venv ai/.venv
ai/.venv/Scripts/python -m pip install -e "./ai[dev]"
```

Ollama is optional, for `ai.generate_text`.

### Build and test

From Git Bash or an MSYS2 UCRT64 shell, with the UCRT64 tools first on `PATH`:

```sh
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -j8
(cd ai && .venv/Scripts/python -m pytest -q)
```

Presets: `debug`, `release` (RelWithDebInfo), `ci` (warnings are errors) and `package` (the release
files' build, [releasing.md](releasing.md)). Every module can be
switched off (`-DMIKSERSTUDIO_WITH_ENGINE=OFF`, `..._API`, `..._SERVICES`, `..._UI`, `..._PLUGINS`,
`..._MCP`) to build and test one module on its own; `src/app` is built only when all of them but the MCP
bridge are on.

### Run

`start.bat` starts the built app from the checkout (the `release` build if there is one, else the
`debug` build), with MSYS2's libraries on `PATH`. Arguments are passed on:

```bat
start.bat
start.bat D:\clips\a.mp4 --play
```

Or run the program itself:

```sh
build/debug/bin/mikser-studio.exe                               # the studio
build/debug/bin/mikser-studio.exe D:/clips/a.mp4 --play         # import, place on V1, play
build/debug/bin/mikser-studio.exe --headless --api-port 47123   # no window; control it through /v1 (docs/api.md)
```

Stop the headless mode with Ctrl+C (or Ctrl+Break, closing its console, `taskkill /IM
mikser-studio.exe` without `/F` on Windows, or SIGINT/SIGTERM on Linux and macOS): Mikser Studio then
shuts down cleanly, so plugins are deactivated, the AI service is stopped and the API's discovery file
`api.json` is removed. A second Ctrl+C ends it at once; `taskkill /F` ends it without cleaning up.

`--screenshot <png> [--screenshot-delay <ms>]` saves a screenshot of the window and quits, which is
handy for checking UI changes. Debug builds are console programs so the log is visible; set
`QT_FORCE_STDERR_LOGGING=1` in MSYS2 shells.

Every run also writes its log, with the AI service's output in it, to `mikser-studio.log` in the
instance's `logs` folder (`%LOCALAPPDATA%\Mikser Studio Dev\logs` on Windows). Each start moves the
last log to `mikser-studio.1.log`, and so on up to `mikser-studio.4.log`; a log that grows past 10 MB
moves aside the same way.

### Instances

Which Mikser Studio runs decides its name and app id, so its own per-user folders (settings, cache,
logs, the API's discovery file). A build from source is **Mikser Studio Dev**, however it is started:
only the `package` preset's build, which sets `MIKSERSTUDIO_INSTALLED_APP`
([releasing.md](releasing.md)), is the installed app. `MIKSERSTUDIO_INSTANCE` chooses another:

| `MIKSERSTUDIO_INSTANCE` | Name | App id | Used by |
|---|---|---|---|
| unset, in a build from source | Mikser Studio Dev | `io.github.infinitinker.mikserstudio.dev` | the program run directly, the UI demo |
| unset, in the `package` build | Mikser Studio | `io.github.infinitinker.mikserstudio` | the installed app |
| `dev` | Mikser Studio Dev | `io.github.infinitinker.mikserstudio.dev` | `start.bat` |
| `e2e` | Mikser Studio E2E | `io.github.infinitinker.mikserstudio.e2e` | the end-to-end tests |

So the app run from source or under test never touches an installed Mikser Studio's files. On
Windows its folder is `%LOCALAPPDATA%\Mikser Studio Dev` or `%LOCALAPPDATA%\Mikser Studio E2E`; the UI
demo keeps its files in the `ui-demo` folder of Mikser Studio Dev's. Any other value of
`MIKSERSTUDIO_INSTANCE` stops Mikser Studio with a message that says what to set.

The per-user folders come from `core::UserFolders`, which the environment can override:
`MIKSERSTUDIO_CONFIG_DIR` (settings), `MIKSERSTUDIO_DATA_DIR` (the discovery file and the logs) and
`MIKSERSTUDIO_CACHE_DIR` (the cache).

Other environment overrides: `MIKSERSTUDIO_AI_DIR`,
`MIKSERSTUDIO_MLT_REPOSITORY`, `MIKSERSTUDIO_MLT_DATA`, `MIKSERSTUDIO_MONITOR_RHI` (`d3d11|d3d12|vulkan|opengl|metal|null`),
`MIKSERSTUDIO_AUDIO_OUTPUT` (`rtaudio|sdl2_audio|silent`).

Demo programs, for working on one module without the others:

- `mikser-studio-monitor-demo <file>`: the GPU monitor and MLT playback alone.
- `mikser-studio-ui-demo [--screenshot <png>]`: the shell with fake media, transport and AI.

## Checks

Run them before committing, from Git Bash with the UCRT64 tools first on `PATH` (as above). The
`Checks` workflow runs the same for every push to `main` and `dev` and every pull request, one job per
group.

**Tests** (job `Tests`): the build with the `ci` preset, where warnings are errors, its tests, and the
AI service's tests.

```sh
cmake --preset ci && cmake --build --preset ci
ctest --preset ci -j8
(cd ai && .venv/Scripts/python -m pytest -q)
```

**C++ checks** (job `C++ checks`): clang-format and clang-tidy, set up by `.clang-format` and
`.clang-tidy` (with `tests/.clang-tidy` for the tests), both failing on any finding. clang-tidy reads
the `ci` preset's compile commands and Qt's generated headers, so configure and generate them first.

```sh
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror
cmake --preset ci && cmake --build --preset ci --target autogen
git ls-files '*.cpp' | xargs -P 8 -n 1 clang-tidy -p build/ci --quiet
```

`clang-format -i <files>` formats files in place.

**Lint** (job `Lint`): Ruff, for the AI service and the scripts in `tools/`; the AI service's import
contracts; the C++ include directions ("Import directions" in [architecture.md](architecture.md));
and the tests of the scripts in `tools/` (`tools/tests/`), with the AI service's pytest settings.

```sh
cd ai
.venv/Scripts/python -m ruff check . ../tools
.venv/Scripts/python -m ruff format --check . ../tools
.venv/Scripts/lint-imports
.venv/Scripts/python -m pytest -c pyproject.toml ../tools/tests
cd ..
ai/.venv/Scripts/python tools/check_includes.py
```

**Types** (job `Types`): mypy, strict, for the AI service and `tools/`.

```sh
cd ai
.venv/Scripts/python -m mypy
.venv/Scripts/python -m mypy ../tools
```

**Licenses** (job `Licenses`): `THIRD-PARTY-NOTICES.md` is up to date, and every license a release
ships can be combined with Mikser Studio's GPL-3.0-or-later ([releasing.md](releasing.md#licenses)).
Both read MSYS2's package database.

```sh
python tools/notices.py --check
python tools/licenses.py
```

## End-to-end tests

`tests/e2e/` starts the built `mikser-studio.exe` headless, as the E2E instance with its settings and
cache in a temporary folder, finds it through its discovery file, drives it through the local API,
asks it to close, and checks that it shut down cleanly and removed the file. It drives the app through
the built MCP bridge `mikser-studio-mcp` too, as an agent's client does. It also starts the real
AI service from `ai/.venv` through the services module and upscales a clip with it; those tests skip
when `ai/.venv` is not set up, and the upscale also needs `ffmpeg` on `PATH`. The tests carry the
`e2e` label, which the `debug` and `ci` test presets leave out; the `e2e` preset runs them, on the
`debug` build:

```sh
cmake --build --preset debug
ctest --preset e2e
```

The `E2E tests` workflow runs them for every push to `main` and `dev` and every pull request.

## GPU test

`tests/engine/gpu/` plays the sample video (`MIKSERSTUDIO_SAMPLE_VIDEO`) in a docked monitor on the
machine's own GPU, floats and re-docks it, and checks the picture the monitor drew. The other engine
tests draw with Qt's offscreen Null backend, so this is the one that needs a GPU and a desktop; no
workflow runs it. It carries the `gpu` label, which the `debug` and `ci` test presets leave out; the
`gpu` preset runs it:

```sh
cmake --build --preset debug
ctest --preset gpu
```

## The icon

`src/app/resources/icon.svg` is the app's icon, built into the program; it is a placeholder until the
real icon is drawn. `docs/images/icon.png`, which the README shows, is rendered from it at 256 pixels
with MSYS2's `rsvg-convert` (`mingw-w64-ucrt-x86_64-librsvg`):

```sh
rsvg-convert -w 256 -h 256 src/app/resources/icon.svg -o docs/images/icon.png
```

## Linux and macOS

The code is portable (QRhi picks Vulkan/OpenGL or Metal; KDDockWidgets, MLT and RtAudio are
cross-platform) but only the Windows build has been run so far. The same packages exist in the
usual package managers; CI for Linux and macOS comes with their ports.

The AI service's environment is made the same way, with `bin/` in place of `Scripts/`:

```sh
python3.13 -m venv ai/.venv
ai/.venv/bin/python -m pip install -e "./ai[dev]"
```

## Branches

`main` is stable and the default branch; work happens on `dev`, and changes reach `main` only through
pull requests from `dev`, merged with a merge commit. The `Checks` jobs (`Lint`, `Types`, `Tests`,
`Licenses`, `C++ checks`) have short, fixed names so the main branch's ruleset can require them.
