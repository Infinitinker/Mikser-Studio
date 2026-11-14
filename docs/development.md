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
  mingw-w64-ucrt-x86_64-{libebur128,fftw,libsamplerate,sox,rubberband,rnnoise,lilv} \
  mingw-w64-ucrt-x86_64-{lua54,sol2,lua54-luafilesystem,lua54-cjson}
```

The fourth line is MLT's optional runtime libraries. Without them MLT silently skips modules: `qt6`
(the `qtblend` compositor), `plus`, `resample`, `sox`, `rubberband`, `rnnoise` and `ladspa`.

The last line is Lua 5.4 with sol2, the header-only C++ binding the Lua host (`src/lua/`) is built
on, and LuaFileSystem (`lfs`) and lua-cjson. Mikser Studio links Lua (`lua54.dll`) and runs its
plugins in it. The scripts in `tools/` (the
release build, the notices, the license and include checks, and what they read from MSYS2) are Lua,
run with `lua5.4` from the repository's root, and never ship; their tests in `tools/tests/` run with
the C++ tests, so configuring with the tests on needs `lua5.4` too.

The C++ checks need clang-format and clang-tidy:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{clang,clang-tools-extra}
```

The checks of the scripts in `tools/` are [StyLua](https://github.com/JohnnyMorganz/StyLua) and
[selene](https://github.com/Kampfkarren/selene), two single programs to put on `PATH`. Both must
read Lua 5.4: a plain `cargo install` builds them without it, so build StyLua with
`--features lua54` and selene with its Lua 5.2 to 5.4 features.

Ollama and ComfyUI are optional: the Ollama plugin and the AI commands use them when they run
([ai.md](ai.md)), and the tests use fakes of them.

### Build and test

From Git Bash or an MSYS2 UCRT64 shell, with the UCRT64 tools first on `PATH`:

```sh
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake --preset debug
cmake --build --preset debug
ctest --preset debug -j8
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
shuts down cleanly, so plugins are deactivated, running AI jobs are canceled and the API's discovery file
`api.json` is removed. A second Ctrl+C ends it at once; `taskkill /F` ends it without cleaning up.

`--screenshot <png> [--screenshot-delay <ms>]` saves a screenshot of the window and quits, which is
handy for checking UI changes. Debug builds are console programs so the log is visible; set
`QT_FORCE_STDERR_LOGGING=1` in MSYS2 shells.

Every run also writes its log to `mikser-studio.log` in the
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

Other environment overrides:
`MIKSERSTUDIO_MLT_REPOSITORY`, `MIKSERSTUDIO_MLT_DATA`, `MIKSERSTUDIO_MONITOR_RHI` (`d3d11|d3d12|vulkan|opengl|metal|null`),
`MIKSERSTUDIO_AUDIO_OUTPUT` (`rtaudio|sdl2_audio|silent`).

Demo programs, for working on one module without the others:

- `mikser-studio-monitor-demo <file>`: the GPU monitor and MLT playback alone.
- `mikser-studio-ui-demo [--screenshot <png>]`: the shell with fake media, transport and AI.

## Checks

Run them before committing, from Git Bash with the UCRT64 tools first on `PATH` (as above). The
`Checks` workflow runs the same for every push to `main` and `dev` and every pull request, one job per
group.

**Tests** (job `Tests`): the build with the `ci` preset, where warnings are errors, and its tests,
the MCP bridge's among them. Tests that measure wall-clock time, tagged `[timing]` in Catch2 and
labeled `timing` in CTest, fail on a busy machine, so the `debug` and `ci` test presets leave them out.
The `timing` preset runs them afterwards, one at a time, and runs a failed one up to twice more.

```sh
cmake --preset ci && cmake --build --preset ci
ctest --preset ci -j8
ctest --preset timing
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

The tests of the scripts in `tools/` are plain Lua scripts, `tools/tests/test_<script>.lua`, which
CTest runs with the C++ tests, labeled `tools` (`ctest --preset ci -L tools` runs them alone). Each
also runs on its own: `lua5.4 tools/tests/test_licenses.lua`.

**Lint** (job `Lint`): the C++ include directions ("Import directions" in
[architecture.md](architecture.md)).

```sh
lua5.4 tools/check_includes.lua
```

**Lua** (no job yet): StyLua and selene, both failing on any finding, for the scripts in `tools/`,
with the settings in `tools/stylua.toml` and `tools/selene.toml` (and Lua 5.4's standard library for
selene in `tools/lua54.yml`), and for the plugins, with those in `plugins/stylua.toml` and
`plugins/selene.toml` (and what the plugins' sandbox offers in `plugins/mikserstudio.yml`).
`stylua .` formats the files in place.

```sh
(cd tools && stylua --check . && selene .)
(cd plugins && stylua --check . && selene .)
```

The plugins' types for Lua language servers, `plugins/types/ms.lua`, are what
`mikser-studio --print-lua-types plugins/types/ms.lua` writes; a Lua host test fails when they are
stale.

**Licenses** (job `Licenses`): `THIRD-PARTY-NOTICES.md` is up to date, and every license a release
ships can be combined with Mikser Studio's GPL-3.0-or-later ([releasing.md](releasing.md#licenses)).
Both read MSYS2's package database.

```sh
lua5.4 tools/notices.lua --check
lua5.4 tools/licenses.lua
```

## End-to-end tests

`tests/e2e/` starts the built `mikser-studio.exe` headless, as the E2E instance with its settings and
cache in a temporary folder, finds it through its discovery file, drives it through the local API,
asks it to close, and checks that it shut down cleanly and removed the file. It drives the app through
the built MCP bridge `mikser-studio-mcp` too, as an agent's client does. It also runs
`ai.run image.generate` against a fake ComfyUI in the test process and checks that the image comes
into the project with its provenance, so no AI tool is needed. The tests carry the
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

The scripts in `tools/` need Lua 5.4 there too, with LuaFileSystem; the include check and
`tools/build_release.lua --version` need Lua alone (on Ubuntu, the `lua5.4` package).

## Branches

`main` is stable and the default branch; work happens on `dev`, and changes reach `main` only through
pull requests from `dev`, merged with a merge commit. The `Checks` jobs (`Lint`, `Tests`, `Licenses`,
`C++ checks`) have short, fixed names so the main branch's ruleset can require them.
