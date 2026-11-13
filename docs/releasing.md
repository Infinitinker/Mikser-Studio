# Releasing

How a release of Mikser Studio is built and published. Windows is the only system with a release for
now; Linux and macOS get one when their ports land. Setting up from source and the checks are in
[development.md](development.md).

## Building a release

`tools/build_release.py` builds three files into `build\dist\`, all uploaded to the release's page on
GitHub, under the tag `v<version>`:

- `MikserStudio-<version>-windows-x64-setup.exe`: the installer, made by
  [Inno Setup](https://jrsoftware.org/isinfo.php) 7 from `tools/installer.iss`. It installs the same
  files as the zip, for the current user into `%LOCALAPPDATA%\Programs\Mikser Studio` with no
  administrator rights, or for all users into Program Files. Setup and Uninstall offer to close a
  running Mikser Studio, and uninstalling an install for the current user asks whether to delete their
  settings, layouts and cache in `%LOCALAPPDATA%\Mikser Studio` too; removing an install for all users
  leaves each user's own folder.
- `MikserStudio-<version>-windows-x64.zip`: `bin\mikser-studio.exe` with the bundled plugins in
  `bin\plugins` and the MCP bridge `bin\mikser-studio-mcp.exe` ([api.md](api.md#mcp-bridge)),
  everything it needs from MSYS2 (the C++ run time, Qt with its plugins and QML modules,
  KDDockWidgets, MLT with its modules in `lib\mlt` and its data in `share\mlt`, FFmpeg and the
  libraries they need), `LICENSE.txt`, Mikser Studio's license, and `THIRD-PARTY-NOTICES.txt`, the
  committed `THIRD-PARTY-NOTICES.md`.
- `MikserStudio-<version>-third-party-notices.md`: the same notices, to read without downloading the
  app.

It needs the package build (the `package` preset, in `build/package`), MSYS2's packages as
[development.md](development.md#windows-msys2-ucrt64) lists them, and Inno Setup 7, found where its
installer puts it or wherever the `ISCC` environment variable points. From Git Bash or an MSYS2 UCRT64
shell, with the UCRT64 tools first on `PATH`:

```sh
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake --preset package
cmake --build --preset package
python tools/build_release.py
```

The `package` preset is the `release` build with `MIKSERSTUDIO_INSTALLED_APP` on and without the
tests: its program is the installed Mikser Studio when `MIKSERSTUDIO_INSTANCE` is unset. Every other
build, `release` included, runs as Mikser Studio Dev, with folders of its own, so a build from source
never touches an installed copy's files; `build_release.py` refuses a build without the option. Don't
run `build/package` from source: it would use the installed app's settings and files.

The version is the one in `CMakeLists.txt`'s `project()`, Mikser Studio's only version.

The AI service (`ai/`) is not part of a release yet: a release has no Python, so its AI commands are
unavailable until the service ships with it.

## Dependencies and notices

A release takes its libraries from MSYS2's UCRT64 environment. `tools/msys2_packages.py` works out
which: the libraries the programs and the bundled plugins import (`PROGRAM_LIBRARIES`), the
MLT modules and Qt plugins a release copies (`MLT_MODULES` and `COPIED`), the libraries those load by
name (`LOADED_LIBRARIES`), and every library all of these import in turn, read from the files
themselves; then the MSYS2 packages they come from, with their licenses, from MSYS2's package database.
`tools/build_release.py` fails unless `THIRD-PARTY-NOTICES.md` covers exactly what it copies: when
the program or a plugin imports a library `PROGRAM_LIBRARIES` does not list, or when the committed
notices are not what `tools/notices.py` writes from the MSYS2 it builds with. So nothing ships without
its notice, and the `Build Windows app` workflow checks the notices before it builds.

`tools/notices.py` writes the license texts of those packages into `THIRD-PARTY-NOTICES.md`. After
adding or upgrading a library, or updating MSYS2, write the notices again and commit the file:

```sh
python tools/notices.py
```

`python tools/notices.py --check`, which the `Licenses` check runs, fails unless the file is up to
date. A package that installs no license text gets the standard text of the license MSYS2 names for
it, unless that text is a template still waiting for the project's own copyright line, as BSD's and
MIT's are. One with neither is listed in `NO_TEXT` in `tools/notices.py`, with the reason, and shows in the
notices as a gap; its text goes in `tools/licenses/<package>.txt`, from the project's source, after
which it leaves the list.

TODO: fourteen packages are in `NO_TEXT` now (among them x264, x265, xvidcore, LAME, ICU and
libvpx): their texts are still to be added to `tools/licenses/`.

## Licenses

Mikser Studio is under the GPL-3.0-or-later (`LICENSE`). MSYS2 builds FFmpeg under the
GPL-3.0-or-later (with x264, x265 and xvidcore in it), Qt HttpServer under the GPL-3.0, and
KDDockWidgets under the GPL-2.0 or the GPL-3.0; MLT is under the LGPL-2.1-or-later. A release is under
the GPL-3.0 as a whole.

Every license of what a release ships must pass `tools/licenses.py`, which the `Licenses` check runs:
one alternative of each package's license must name only licenses on its allow list (`ALLOWED`), those
that can be combined with the GPL-3.0-or-later. A package whose metadata names no SPDX license, or
joins alternatives with AND, needs an entry in `EXCEPTIONS`, with the license as checked by hand.

### The source of a release

The GPL and the LGPL ask a release to offer the source of what it ships under them. Each release is
to carry `MikserStudio-<version>-sources.zip`: Mikser Studio's source at the release's commit, and the
source of each MSYS2 package it ships under the GPL or the LGPL, at the shipped version (MSYS2's source
packages).

TODO: the sources zip is not built yet. Mikser Studio's own source is small (`git archive`), but the
packages' sources, FFmpeg's and Qt's among them, run to hundreds of megabytes. Next step:
`tools/build_release.py` writes the zip from `git archive` and MSYS2's source packages, and the
release workflows check and publish it as a fourth file.

## Building on GitHub

Releases are built on GitHub the same way, on a clean Windows runner, by the `Build Windows app`
workflow (`.github/workflows/build-windows.yml`): it installs MSYS2's packages and Inno Setup, builds
the `package` preset and runs `tools/build_release.py`, then checks that exactly the three release
files arrived, under the version's name, with a whole zip (the `Windows release files` job). It runs
for every pull request, not for pushes, keeping the files on the run's page, so a change that breaks
the release shows up before it reaches `main`. To build `dev` without a pull request, run it by hand
(Actions > Build Windows app > Run workflow).

## Publishing a release

The `Release` workflow (`.github/workflows/release.yml`) checks the version and calls that build:

1. Raise the version in `CMakeLists.txt`'s `project()`.
2. Before tagging, run the workflow by hand on `dev` (Actions > Release > Run workflow): it does
   everything but the draft, so a change to either workflow shows up before the tag does, and keeps
   the files on the run's page.
3. Merge `dev` into `main` with a pull request, then push a tag `v<version>` on `main`, where
   `<version>` is the version in `CMakeLists.txt`. The workflow builds the files, records where each
   was built (a provenance attestation, which
   `gh attestation verify <file> --repo Infinitinker/Mikser-Studio` checks), and drafts the release's
   page with the three files and their checksums. A tag that differs from the version, or is not on
   `main`, fails before anything is built.
4. Write the release notes on the draft, then publish it.
