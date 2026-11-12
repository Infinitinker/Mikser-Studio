# Development

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

Presets: `debug`, `release` (RelWithDebInfo) and `ci` (warnings are errors). Every module can be
switched off (`-DMIKSERSTUDIO_WITH_ENGINE=OFF`, `..._API`, `..._SERVICES`, `..._UI`, `..._PLUGINS`) to build
and test one module on its own; `src/app` is built only when all of them are on.

### Run

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

Environment overrides: `MIKSERSTUDIO_CONFIG_DIR`, `MIKSERSTUDIO_CACHE_DIR`, `MIKSERSTUDIO_AI_DIR`,
`MIKSERSTUDIO_MLT_REPOSITORY`, `MIKSERSTUDIO_MLT_DATA`, `MIKSERSTUDIO_MONITOR_RHI` (`d3d11|d3d12|vulkan|opengl|metal|null`),
`MIKSERSTUDIO_AUDIO_OUTPUT` (`rtaudio|sdl2_audio|silent`).

Demo programs, for working on one module without the others:

- `mikser-studio-monitor-demo <file> [--self-test <s> <png>]`: the GPU monitor and MLT playback alone.
- `mikser-studio-ui-demo [--screenshot <png>]`: the shell with fake media, transport and AI.

## Linux and macOS

The code is portable (QRhi picks Vulkan/OpenGL or Metal; KDDockWidgets, MLT and RtAudio are
cross-platform) but only the Windows build has been run so far. The same packages exist in the
usual package managers; CI for all three platforms is still to be set up.

The AI service's environment is made the same way, with `bin/` in place of `Scripts/`:

```sh
python3.13 -m venv ai/.venv
ai/.venv/bin/python -m pip install -e "./ai[dev]"
```
