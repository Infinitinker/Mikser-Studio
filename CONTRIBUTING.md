# Contributing to Mikser Studio

Thank you for helping! Plugins are the most welcome contribution: Mikser Studio is built so that new
tools come as plugins, without changes to the app itself. Fixes, ideas and bug reports are welcome too.

- **Found a bug?** Open an issue with the bug report form, with the steps that lead to it.
- **Missing a tool, or something a plugin needs?** Open an issue with the plugin request form.
- **Found a security problem?** Report it privately, as [SECURITY.md](SECURITY.md) describes, not in
  an issue.

Everything in the project is written in English, with American spelling (color, canceled, license).

## Contributing a plugin

A plugin is a folder with a `mikserstudio-plugin.json` manifest and Lua code, and optionally QML
panels. It adds commands, which work from the menus, the command palette, the local API and other
plugins alike, settings, whose form Mikser Studio draws from a JSON Schema, and panels.
[docs/plugins.md](docs/plugins.md) explains the manifest, the Lua API, permissions, settings and
panels, and `plugins/examples/hello/` is a complete example. Nothing is built: a plugin is its files.

The steps below make a plugin by copying the example. Choose its name first: lowercase letters, digits
and underscores, after what the plugin does, like `word_count`. Below, `word_count` stands for your
plugin's name and `yourname` for your GitHub name, also in lowercase letters, digits and underscores.
You need a text editor and Mikser Studio built from source ([docs/development.md](docs/development.md)),
which reloads a plugin when you save one of its files.

1. **Copy the example.** Copy the folder `plugins/examples/hello` to a folder of your own, such as
   `D:\plugins\word_count`. The folder's name doesn't matter to Mikser Studio, but keeping it the
   plugin's name helps.
2. **Give it its own id.** In `mikserstudio-plugin.json`, change two lines:
   - `"id": "mikserstudio.examples.hello"` to `"id": "yourname.word_count"`;
   - `"name": "Hello"` to the name people see, such as `"name": "Word count"`, and the
     `"description"` line below it to a sentence saying what the plugin does.
3. **Give its command the same id.** In `main.lua`, change
   `id = "mikserstudio.examples.hello.greet"` to `id = "yourname.word_count.greet"`: every command's
   id starts with its plugin's id. Change its `title` and `description` too; the title is what the
   command palette and the menus show. In `spec/greet_spec.lua`, the spec that tests it, change the
   ids at the top the same way. The panel, `hello.qml`, finds the command through the plugin's id,
   so it needs no change.
4. **Try it.** Start Mikser Studio with `MIKSERSTUDIO_PLUGIN_PATH` naming the folder that holds your
   plugin's folder (`D:\plugins` above): `set MIKSERSTUDIO_PLUGIN_PATH=D:\plugins`, then `start.bat`.
   Open the command palette (Ctrl+K), type your command's title and run it; the panel is in the
   Window menu. A plugin with a mistake, such as a wrong id, is listed in the Plugins panel as
   failed, with the reason, and never takes the app down.
5. **Make it yours.** Now change what the command does (its `handler` in `main.lua`), its settings
   (`settingsSchema` in the manifest), its spec and the panel. [docs/plugins.md](docs/plugins.md)
   explains each of them; rename `hello.qml` if you like (the panel's file name is `"qml"` in the
   manifest's `panels`). Each save reloads the plugin.
6. **Share it.** Open a pull request that adds it to Mikser Studio as `plugins/builtin/word_count`,
   where `ctest` runs its specs, or link it from a plugin request issue. A plugin someone puts in
   their own `plugins` folder (`%LOCALAPPDATA%\Mikser Studio\plugins` on Windows) stays off until
   they turn it on in the Plugins panel, and its panels are not loaded there: only built-in plugins
   and plugins in development may have QML panels.

## Code

[docs/development.md](docs/development.md) has the setup on Windows, how to run Mikser Studio from
source, every check with its command, and the end-to-end tests;
[docs/architecture.md](docs/architecture.md) explains how the code is organized, which module may
include which, and the names the project uses for its concepts.

1. Work on a branch from `dev`, and open the pull request to `dev`. `main` gets the changes from `dev`
   through a pull request.
2. Run the checks before pushing, as "Checks" in [docs/development.md](docs/development.md) lists
   them: the build and its tests, clang-format, clang-tidy and `tools/check_includes.lua` for the C++
   code; StyLua and selene for the scripts in `tools/` and the plugins. The `Checks` workflow runs all
   but StyLua and selene for every pull request, and the `E2E tests` workflow the end-to-end tests.
3. Modules include only what [docs/architecture.md](docs/architecture.md#import-directions) allows:
   plugins see only the Lua API, and the app's modules only the core. A new dependency is justified and its
   license checked before it is added; after adding or upgrading a library a release ships,
   `tools/notices.lua` writes `THIRD-PARTY-NOTICES.md` again ([docs/releasing.md](docs/releasing.md)).
4. Tests live in `tests/`, mirroring `src/` (the tests for `src/core` are in `tests/core`), together
   with their fakes; nothing test-only goes in `src/`. A plugin's tests are Lua specs in its own
   `spec/` folder, which `tests/plugins` runs.
   They run without the network; a sound device is never needed, as MLT falls back to its silent
   output. The end-to-end tests in `tests/e2e/` and the GPU test in `tests/engine/gpu/` run apart
   ([docs/development.md](docs/development.md)).
5. Update the docs in the same pull request as the code they describe. Commits follow
   [Conventional Commits](https://www.conventionalcommits.org/), with a scope from CLAUDE.md, such as
   `fix(engine): keep the monitor in sync`.
