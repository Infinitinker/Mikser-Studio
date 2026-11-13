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

The steps below make a plugin by copying the example. Its name is used in three places, so choose it
first: lowercase letters, digits and underscores, after what the plugin does, like `word_count`. Below,
`word_count` stands for your plugin's name and `yourname` for your GitHub name, also in lowercase letters,
digits and underscores. You need a text editor; nothing else in Mikser Studio changes.

1. **Copy the example.** Copy the folder `plugins/hello` to `plugins/word_count`. The folder's name is
   the plugin's name: the build makes `word_count.dll` from it and puts it in
   `bin/plugins/word_count/`, so there is nothing to change in any `CMakeLists.txt`.
2. **Give it its own id.** In `plugins/word_count/mikserstudio-plugin.json`, change three lines:
   - `"id": "mikserstudio.examples.hello"` to `"id": "yourname.word_count"`;
   - `"name": "Hello"` to the name people see, such as `"name": "Word count"`, and the
     `"description"` line below it to a sentence saying what the plugin does;
   - `"entry": "hello"` to `"entry": "word_count"`, the folder's name.
3. **Give its command the same id.** In `plugins/word_count/src/HelloPlugin.cpp`, near the top,
   change `"id": "mikserstudio.examples.hello.greet"` to `"id": "yourname.word_count.greet"`: every
   command's id starts with its plugin's id. Change its `"title"` and `"description"` too; the title is
   what the command palette and the menus show. The panel, `hello.qml`, finds the command through the
   plugin's id, so it needs no change.
4. **Build and try it.** Build Mikser Studio as [docs/development.md](docs/development.md) describes,
   then start it with `start.bat`. Open the command palette (Ctrl+K), type your command's title and
   run it; the panel is in the Window menu. A plugin with a mistake, such as a wrong id, is listed in
   the Plugins panel as failed, with the reason, and never takes the app down.
5. **Make it yours.** Now change what the command does (the `greet` function in `HelloPlugin.cpp`),
   its settings (`settingsSchema` in the manifest) and the panel. [docs/plugins.md](docs/plugins.md)
   explains each of them; rename `HelloPlugin.cpp` and `hello.qml` if you like (the panel's file name
   is `"qml"` in the manifest's `panels`).
6. **Share it.** Open a pull request that adds `plugins/word_count` to Mikser Studio, or link it from
   a plugin request issue.

## Code

[docs/development.md](docs/development.md) has the setup on Windows, how to run Mikser Studio from
source, every check with its command, and the end-to-end tests;
[docs/architecture.md](docs/architecture.md) explains how the code is organized, which module may
include which, and the names the project uses for its concepts.

1. Work on a branch from `dev`, and open the pull request to `dev`. `main` gets the changes from `dev`
   through a pull request.
2. Run the checks before pushing, as "Checks" in [docs/development.md](docs/development.md) lists
   them: the build and its tests, clang-format, clang-tidy and `tools/check_includes.py` for the C++
   code; Ruff, mypy, pytest and `lint-imports` for the MCP bridge and `tools/`. The `Checks` workflow runs them for
   every pull request, and the `E2E tests` workflow the end-to-end tests.
3. Modules include only what [docs/architecture.md](docs/architecture.md#import-directions) allows:
   plugins see only the SDK, and the app's modules only the core. A new dependency is justified and its
   license checked before it is added; after adding or upgrading a library a release ships,
   `tools/notices.py` writes `THIRD-PARTY-NOTICES.md` again ([docs/releasing.md](docs/releasing.md)).
4. Tests live in `tests/`, mirroring `src/` (the tests for `src/core` are in `tests/core`) and
   `plugins/` (`tests/plugins/hello`), together with their fakes; nothing test-only goes in `src/`.
   They run without the network; a sound device is never needed, as MLT falls back to its silent
   output. The MCP bridge's tests are in `ai/tests/`, mirroring `ai/src/mikserstudio_ai/`. The
   end-to-end tests in `tests/e2e/` and the GPU test in `tests/engine/gpu/` run apart
   ([docs/development.md](docs/development.md)).
5. Update the docs in the same pull request as the code they describe. Commits follow
   [Conventional Commits](https://www.conventionalcommits.org/), with a scope from CLAUDE.md, such as
   `fix(engine): keep the monitor in sync`.
