# Contributing to Mikser Studio

Thank you for helping! Plugins are the most welcome contribution: Mikser Studio is built so that new
tools come as plugins, without changes to the app itself. Fixes, ideas and bug reports are welcome too.

- **Found a bug?** Open an issue with the bug report form, with the steps that lead to it.
- **Missing a tool, or something a plugin needs?** Open an issue with the plugin request form.
- **Found a security problem?** Report it privately, as [SECURITY.md](SECURITY.md) describes, not in
  an issue.

Everything in the project is written in English, with American spelling (color, canceled, license).

## Contributing a plugin

A plugin is a folder with a `mikserstudio-plugin.json` manifest and a shared library, and optionally
QML panels. It adds commands, which work from the menus, the command palette, the local API and other
plugins alike, settings, whose form Mikser Studio draws from a JSON Schema, and panels.
[docs/plugins.md](docs/plugins.md) explains the manifest, the C interface, settings and panels, and
`plugins/hello/` is a complete example.

1. Copy `plugins/hello/` to `plugins/<name>/`, add it to `plugins/CMakeLists.txt`, and give its
   manifest an id of your own.
2. Build Mikser Studio as [docs/development.md](docs/development.md) describes: the plugin is built
   with it, into `bin/plugins/<name>/`, where Mikser Studio finds it.
3. Start Mikser Studio and try the plugin's commands from the command palette. A plugin with a
   mistake is listed as failed, with the reason, and never takes the app down.
4. Share it: open a pull request that adds it to `plugins/`, or link it from a plugin request issue.

Name the folder after what the plugin does, in lowercase with dashes, like `scene-detect`.

## Code

[docs/development.md](docs/development.md) has the setup on Windows, how to run Mikser Studio from
source, every check with its command, and the end-to-end tests;
[docs/architecture.md](docs/architecture.md) explains how the code is organized, which module may
include which, and the names the project uses for its concepts.

1. Work on a branch from `dev`, and open the pull request to `dev`. `main` gets the changes from `dev`
   through a pull request.
2. Run the checks before pushing, as "Checks" in [docs/development.md](docs/development.md) lists
   them: the build and its tests, clang-format, clang-tidy and `tools/check_includes.py` for the C++
   code; Ruff, mypy, pytest and `lint-imports` for the AI service. The `Checks` workflow runs them for
   every pull request, and the `E2E tests` workflow the end-to-end tests.
3. Modules include only what [docs/architecture.md](docs/architecture.md#import-directions) allows:
   plugins see only the SDK, and the app's modules only the core. A new dependency is justified and its
   license checked before it is added; after adding or upgrading a library a release ships,
   `tools/notices.py` writes `THIRD-PARTY-NOTICES.md` again ([docs/releasing.md](docs/releasing.md)).
4. Tests live in `tests/`, mirroring `src/` (the tests for `src/core` are in `tests/core`), together
   with their fakes; nothing test-only goes in `src/`. They run without the network; a sound device is
   never needed, as MLT falls back to its silent output. The AI service's tests are in `ai/tests/`,
   and the end-to-end tests in `tests/e2e/`.
5. Update the docs in the same pull request as the code they describe. Commits follow
   [Conventional Commits](https://www.conventionalcommits.org/), with a scope from CLAUDE.md, such as
   `fix(engine): keep the monitor in sync`.
