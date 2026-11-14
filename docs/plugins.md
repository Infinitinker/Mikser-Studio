# Writing a plugin

A Mikser Studio plugin is a folder with a manifest, `mikserstudio-plugin.json`, and Lua code. It adds
commands, which work from the menus, the command palette, the local API, the MCP bridge and other
plugins alike; settings, whose form the app draws from a JSON Schema; and, for built-in plugins and
plugins in development, QML panels. Nothing is compiled, and a build from source reloads a plugin
when one of its files changes.

`plugins/examples/hello/` is a complete example: a command, a setting and a panel.
[CONTRIBUTING.md](../CONTRIBUTING.md) walks through making a plugin from it.

## Layout and discovery

```
word_count/
  mikserstudio-plugin.json   the manifest
  main.lua                   the entry file, run when the plugin starts
  lib/counting.lua           more code, loaded with require("lib.counting")
  panel.qml                  a panel (built-in plugins and plugins in development only)
  spec/count_spec.lua        a test, never installed (see Tests)
```

Mikser Studio looks for plugins in three places. A folder in one of them with a manifest is a
plugin; when two have the same id, the first one found is used and the other is ignored, with a
warning in the log.

- **Built-in plugins**, in `plugins/` next to the program (`plugins/builtin/<name>/` in the
  repository). They run with the permissions their manifests ask for.
- **The person's own plugins**, in the `plugins` folder of the app's data folder
  (`%LOCALAPPDATA%\Mikser Studio\plugins` on Windows). They stay off until the person turns them on
  in the Plugins panel, which grants them the permissions their manifests ask for, except
  `plugins.manage`.
- **Plugins in development**, in the folders `MIKSERSTUDIO_PLUGIN_PATH` names (separated like
  `PATH`, with `;` on Windows). They are on, with the permissions of the person's own plugins.

`mikser-studio --safe-mode` runs only the built-in plugins. A plugin that fails never takes the app
down: the Plugins panel lists it as failed, with the reason, and the log has the details.

## Manifest

```json
{
    "id": "yourname.word_count",
    "name": "Word count",
    "version": "0.1.0",
    "description": "Counts the words in the project's titles.",
    "apiVersion": 1,
    "entry": "main.lua",
    "permissions": ["project.read"],
    "settingsSchema": {
        "type": "object",
        "properties": {
            "minimum": {"type": "integer", "title": "Shortest word", "minimum": 1, "default": 1}
        },
        "additionalProperties": false
    },
    "panels": [{"id": "counter", "title": "Word count", "qml": "panel.qml", "defaultArea": "right"}]
}
```

- `id`: dotted lower_snake with at least two parts, such as `yourname.word_count`; built-in plugins
  are `mikserstudio.<name>`. Every command the plugin registers starts with it.
- `name`, `version` (SemVer) and `description` (optional): what the Plugins panel shows.
- `apiVersion`: 1, the version of the Lua API described here.
- `entry`: the Lua file that starts the plugin, inside its folder, with `/` between folders.
- `permissions` (optional): what the plugin may do; see [Permissions](#permissions).
- `network` (optional, with the `network` permission): where the plugin's jobs may connect, as
  `host:port` entries; see [The network](#the-network).
- `settingsSchema` (optional): a JSON Schema of the plugin's settings, an object.
- `panels` (optional): QML panels; see [Panels](#panels-qml).

A manifest with a `kind` is refused: Mikser Studio runs Lua plugins only, and native and Python
plugins are gone. The old `capabilities` is refused too: `permissions` replaced it.

## The Lua API

A plugin's code sees the sandboxed standard library and one global, `ms`. Its entry file runs when
the plugin starts; it registers the plugin's commands and event callbacks, and may run commands
itself.

```lua
ms.command({
    id = "yourname.word_count.count",
    title = "Count words",
    description = "Counts the words in a text.",
    read_only = true,
    params = {
        type = "object",
        properties = { text = { type = "string" } },
        required = { "text" },
    },
    result = { type = "object", properties = { words = { type = "integer" } } },
    handler = function(params, ctx)
        local words = 0
        for _ in params.text:gmatch("%S+") do
            words = words + 1
        end
        return { words = words }
    end,
})
```

- `ms.command{...}` registers a command. Its keys: `id`, `title` and `handler` (required),
  `description`, `category` (the menu group; the plugin's name by default), `params` and `result`
  (JSON Schemas), `permission` (what a caller needs to run it), `read_only`, `confirmation` (a
  question the person answers before each run), `open_world` (it reaches outside the app),
  `starts_job` (its work goes on in a job), `quiet` and `shortcut`. The handler gets the params,
  with the schema's defaults, and `ctx`, the call's `origin` and `origins`; it returns the result
  table, or raises an error.
- `ms.run(id, params)` runs a command and waits for its result without blocking anything. It raises
  an error table with `code` and `message` when the command fails; see [Errors](#errors).
- `ms.on(topic, fn)` calls `fn(payload, topic)` for each event of a topic, such as `clip.added`, or
  of a family, such as `clip.*` ([api.md](api.md#events-websocket-v1events) lists them).
- `ms.transaction(name, fn)` runs `fn`, and the commands it runs make one undo step called `name`.
  An error in `fn` undoes them all and is raised again.
- `ms.jobs.start(kind, input)` starts a job of a kind the app provides and returns it:
  `job:await()` waits for it to end and returns its result, raising an error when it failed or was
  canceled; `job:cancel()` cancels it. A job can't be awaited inside a transaction.
- `ms.jobs.run({kind, title, lane}, fn)` starts a job of the plugin's own, whose work is `fn(job)`:
  it runs as a task of its own once the job starts, what it returns is the job's result, and an
  error it raises fails the job. Its kind is `<plugin id>.<kind>`, its `title` is what the person
  sees, and jobs of one `lane` run one at a time. `job:progress(fraction, message)` shows how far it
  has come. The jobs `fn` starts are part of it: their progress shows on it, and when it ends or is
  canceled, they are canceled. A command whose work takes long returns `{jobId = job.id}` at once
  and marks itself `starts_job`.
- `ms.jobs.get(id)` is the job with that id, to await or cancel, such as one another plugin's command
  started; awaiting it reads `jobs.list`, which needs `project.read`.
- `ms.project()` returns the project as it is now, read-only: `session`, `revision`, `frameRate`
  (`{num, den}`), `width`, `height`, `media`, `tracks` and `clips`, as `project.list` gives them,
  with `find_media(id)`, `find_track(id)`, `find_clip(id)`,
  `clips_in_range(from, to, track_id)` (the clips that overlap the frames `[from, to)`, on one track
  or on all) and `clips_of_media(id)`. It is a copy: call it again after a change.
- `ms.settings()` returns the plugin's settings, with the schema's defaults.
- `ms.log.debug`, `ms.log.info`, `ms.log.warning` and `ms.log.error` write to the app's log, under
  the plugin's id; `print` is `ms.log.info`.
- `ms.array(t)` marks a table as a JSON array, so that an empty one stays `[]`; `ms.null` is JSON's
  `null` inside an array; `ms.plugin_id` is the plugin's id.

Handlers, event callbacks and the entry file each run as a task of their own, so `ms.run`,
`ms.transaction` and `job:await()` wait without holding the plugin's thread: other commands and
events are handled meanwhile. They work only in those tasks, not in a coroutine the plugin makes
itself.

### Job kinds

The app's job kinds, which `ms.jobs.start` starts. Both need the `network` permission and connect
only where the plugin's [network entries](#the-network) allow. Their `body`, when there is one, is
sent as JSON; redirects aren't followed, and answers over 16 MB fail.

- `http.request {url, method = "GET", body, timeout = 30, title}` answers
  `{status, body, json}`: the status and the text of the answer, also of one that isn't 2xx, and
  `json` when the text is JSON. It fails only when the address can't be reached or nothing comes
  back for `timeout` seconds.
- `http.stream_ndjson {url, method = "POST", body, fields = {"response"}, messages, timeout = 600,
  title}` reads an answer of JSON lines, such as Ollama's, and answers `{fields, last, lines}`: the
  text of each of `fields` joined across the lines, the last line, and how many there were.
  `messages` maps a field to the progress shown while it grows, such as
  `{response = "Writing ({count})"}`, with the lines so far for `{count}`. It fails with the tool's
  own message for an answer that isn't 2xx or a line with an `error`, and when nothing comes for
  `timeout` seconds, which allows for a model that takes minutes to load.

### Errors

A command that fails raises a table `{code, message}` in `ms.run`, where `code` is one of:

- `invalid_params`: the params don't match the command's schema;
- `permission_denied`: the plugin lacks the command's permission;
- `needs_confirmation`: the command would replace the person's work, and only the person can agree
  (the message is the question). A plugin can't agree for the person: a parameter it sets to agree,
  such as `overwrite`, is dropped;
- `canceled`: the run was stopped, by its caller, a timeout, or the person declining the command's
  question;
- `not_available`, `unknown_command` or `failed`.

An error a handler raises fails its run. A table with `code` and `message` keeps its code when that
is `invalid_params`, `not_available`, `permission_denied` or `canceled`; any other error is
`failed`, with its message. The person sees the message, and the log gets the traceback.

## Permissions

| Permission | What it allows |
|---|---|
| `project.read` | Reading the project through `ms.project()` and the read-only commands. |
| `project.write` | Running the commands that change the project. |
| `files` | Opening, saving or importing a path outside the project. |
| `network` | Running the commands and jobs that reach the network, such as the AI tools'. |
| `ui` | Contributing panels and driving the monitors and playback. |
| `plugins.manage` | Running the `plugins.*` commands; only built-in plugins get it. |

The permission a command needs belongs to whoever runs it: a plugin needs it itself, also when an
agent asked the plugin to run its command. A plugin's command runs with no more than its caller's
permissions, and the plugin's own.

### The network

A plugin reaches the network only through the app's job kinds, such as `http.request`, and only
where its manifest's `network` entries allow. Each entry is `host:port`:

- the host is a name (`api.example.com`), an IPv4 address or an IPv6 address in brackets
  (`[::1]`), or `*`, any computer but this one;
- the port is a number or `*`, any port.

This computer (`localhost`, `127.0.0.0/8`, `::1`) is reached only through an entry that names it,
such as `127.0.0.1:11434` or `localhost:*`, and Mikser Studio's own API never. Settings can't widen
the entries: a plugin whose address is a setting lists every address the person may enter, such as
`127.0.0.1:*`, `localhost:*`, `[::1]:*` and `*:*` for a tool that runs on this computer or another
one. The entries are checked against the address as it is written; a name that leads to this
computer is not caught.

## The sandbox

Each plugin runs in a Lua 5.4 state of its own, on a thread of its own, never on the interface's
thread. It has the `base`, `string`, `table`, `math`, `utf8` and `coroutine` libraries, and of `os`
only `time`, `clock` and `date`. It has no `io`, `debug`, `package`, `dofile`, `loadfile` or
`string.dump`; `load` takes only text, and `require` loads only Lua files from the plugin's folder
(`require("lib.counting")` is `lib/counting.lua`). Values reach the plugin, and leave it, as JSON
copies.

A state may use 256 MB; more gives "not enough memory" errors. Code that runs 5 seconds without
waiting (for `ms.run`, a job or an event) is stopped with an error: split the work, or run it as a
job.

## Settings

Settings are a JSON object validated against `settingsSchema`, with its defaults filled in, kept in
`<config>/plugin-settings/<plugin-id>.json`. Stored settings that no longer match the schema are
ignored in favor of the defaults. The Plugins panel draws their form from the schema; agents change
them with `plugins.set_settings`. Read them with `ms.settings()` whenever you need them: they can
change while the plugin runs.

## Panels (QML)

A panel is a QML file the interface loads. QML runs JavaScript, with network access, on the
interface's thread, so only built-in plugins and plugins in development, such as the example, may
have panels: the panels of the person's own plugins are not loaded, and the log says so. The panel's
root item gets one context property, `mikserstudio`:

```qml
mikserstudio.execute(commandId: string, params: object, callback: function)
    // Runs the command, which may answer later, and calls callback(reply) once:
    // {ok: true, result: {...}}  or  {ok: false, error: {code, message}}
    // code is one of those in "Errors"; "canceled" also when the person declines the command's
    // question
mikserstudio.settings   // object: the plugin's current settings
mikserstudio.pluginId   // string
```

Use only this contract and the standard `QtQuick`, `QtQuick.Controls` and `QtQuick.Layouts`
modules. Every action of a panel goes through `mikserstudio.execute`, which asks the person when a
command needs it. See `plugins/examples/hello/hello.qml`.

## Hot reload

A build from source, and any build with `MIKSERSTUDIO_PLUGIN_HOT_RELOAD=1`, reloads a plugin when one
of its files changes: its commands are unregistered, its runs in flight finish as `canceled`, its
jobs are canceled, and its entry file runs again in a new state. A reload whose new code fails
leaves the plugin failed, with the reason, until the next change fixes it.

## Tests

A plugin's tests are Lua files in its `spec/` folder, such as
`plugins/examples/hello/spec/greet_spec.lua`. The test `mikserstudio-plugin-specs`
(`tests/plugins/`, run by `ctest`) finds them in the built-in plugins and the example: it starts the
plugin in the Lua host, then runs each spec as a plugin of its own, with every permission. A spec
calls the plugin's commands, and the `plugins.*` commands to change its settings, through
`ms.run`, and checks what they answer; it passes when it runs to its end and fails with the error it
raises.

```lua
local answer = ms.run("yourname.word_count.count", { text = "one two" })
assert(answer.words == 2, "two words")

local ok, problem = pcall(ms.run, "yourname.word_count.count", { text = 3 })
assert(not ok and problem.code == "invalid_params")
```

`require` in a spec loads the plugin's own files, so a spec can also test them one by one. A
plugin that talks to an AI tool is tested against the fake tools of `tests/services`, which the test
starts on this computer: `ms.settings().fakes` holds their addresses (`ollama`, `comfyui`), and a
spec points the plugin's settings at them. A spec has the plugin's network entries, and the job
kinds of the app, such as `http.request`. `spec/` is never staged or installed. The Lua host's own
tests are in `tests/lua/`.

## Tooling

- [StyLua](https://github.com/JohnnyMorganz/StyLua) formats plugins with the settings in
  `plugins/stylua.toml`: four spaces and lines of up to 120 characters.
- [selene](https://github.com/Kampfkarren/selene) checks them against what the sandbox offers and
  the `ms` API, from `plugins/selene.toml` and `plugins/mikserstudio.yml`.
- A Lua language server, such as LuaLS, completes and checks the `ms` API from
  `plugins/types/ms.lua`, which `.luarc.json` at the repository's root points it to. A plugin
  outside the repository gets the same file from `mikser-studio --print-lua-types ms.lua`.

```sh
cd plugins
stylua --check .
selene .
```
