# Mikser Studio local API (`/v1`)

Mikser Studio runs a small HTTP + WebSocket server so that local tools (scripts, the MCP bridge, editor
integrations, test harnesses) can drive the app. It exposes the same command registry that the
menus, shortcuts, the command palette and plugins use: every command listed by the app can be run
through the API, and undo works the same way.

The implementation lives in `src/api` (`MikserStudio::api`, class `mikserstudio::api::ApiServer`).

## Security model

- The server listens **only on loopback**: `127.0.0.1` and, when available, `[::1]`, on the same
  port. It is never reachable from other machines.
- Every route except `GET /v1/health` needs the session token:
  `Authorization: Bearer <token>`. The token is 32 random bytes from the OS CSPRNG
  (base64url, 43 characters), new for every run unless the app passes a fixed one. It is compared in
  constant time.
- The token is published only in the **discovery file**, which is readable only by the current user
  (mode `0600` on Linux/macOS; on Windows a protected ACL with one entry, full control for the
  current user).
- No CORS headers are sent, so web pages cannot read API responses, and they cannot send the
  `Authorization` header without a preflight that fails. The API is meant for local tools, not
  browsers.
- Requests are logged at debug level (category `mikserstudio.api`) as method, path, status and duration.
  The query string and headers are never logged, so the token does not end up in logs. Enable the log
  with `QT_LOGGING_RULES="mikserstudio.api.debug=true"`.

## Discovery file

While the server runs, it writes a JSON file (atomically, with `QSaveFile`):

```json
{
    "apiVersion": 1,
    "baseUrl": "http://127.0.0.1:52341",
    "eventsUrl": "ws://127.0.0.1:52341/v1/events",
    "pid": 4242,
    "port": 52341,
    "token": "q3o7cY7m1nM1yQm8f7b4bVn0v8fP3zJk2dS9xW6tL5A"
}
```

Location: `api.json` in the data folder of the Mikser Studio that runs (`core::UserFolders`,
`QStandardPaths::AppLocalDataLocation` for the instance's name, or `MIKSERSTUDIO_DATA_DIR`). For the
installed app, named `Mikser Studio` (it sets no organization name, so there is no extra folder level):

| OS | Path |
|---|---|
| Windows | `%LOCALAPPDATA%\Mikser Studio\api.json` |
| macOS | `~/Library/Application Support/Mikser Studio/api.json` |
| Linux | `~/.local/share/Mikser Studio/api.json` |

A build from source and the app under the end-to-end tests are named `Mikser Studio Dev` and `Mikser
Studio E2E` (see [development.md](development.md#instances)), so their files are in folders of those
names instead.

The file is removed when the server stops or the app exits normally. If the app crashes the file
stays behind: check that `pid` is still running (or that `GET /v1/health` answers) before trusting
it. When two instances run, the last one to start owns the file; an instance only removes the file if
it still holds its own token.

The examples below read the file with `jq` (Linux/macOS/Git Bash):

```sh
API=$(jq -r .baseUrl "$HOME/.local/share/Mikser Studio/api.json")
TOKEN=$(jq -r .token "$HOME/.local/share/Mikser Studio/api.json")
```

## Conventions

- Requests and responses are JSON (`Content-Type: application/json`), UTF-8.
- Responses carry `Cache-Control: no-store`.
- Errors always have this shape, with an English message that says what to do:

  ```json
  {"error": {"code": "invalid_params", "message": "Invalid parameters for \"project.import_media\": /path: must be a string"}}
  ```

| HTTP | `error.code` | When |
|---|---|---|
| 400 | `invalid_params` | The params do not match the command's `paramsSchema`, or the body is not a JSON object. |
| 400 | `malformed_json` | The body is not valid JSON. |
| 401 | `unauthorized` | Missing or wrong token (also sends `WWW-Authenticate: Bearer`). |
| 403 | `permission_denied` | The caller lacks the command's `permission`. API clients carry every permission, so today only a command a plugin runs for them can answer this. |
| 404 | `unknown_command` | No command with that id. |
| 404 | `not_found` | No such route. |
| 405 | `method_not_allowed` | Wrong HTTP method (the `Allow` header lists the right ones). |
| 409 | `not_available` | The command exists but cannot run now (e.g. nothing to undo). |
| 409 | `needs_confirmation` | This run needs the person's agreement; the message is the question (see `confirmationParam`). |
| 409 | `canceled` | The run was stopped before it finished, such as a command that answers later and didn't within 60 seconds. |
| 413 | `payload_too_large` | The body is larger than 1 MiB (1 048 576 bytes). |
| 426 | `upgrade_required` | A plain GET on `/v1/events`, which is a WebSocket. |
| 500 | `failed` | The command ran and failed; the message says why. |

Bodies larger than 8 MiB are refused by the HTTP layer before they are read: the server answers a
bare `413` (to clients that send `Expect: 100-continue`) or closes the connection. Pass large data as
file paths instead.

Commands run on the app's main thread, one at a time, in the order the requests arrive. A slow
command delays the requests behind it.

## Routes

### `GET /v1/health` (no token)

```sh
curl -s http://127.0.0.1:52341/v1/health
```

```json
{"apiVersion": 1, "name": "Mikser Studio", "version": "0.1.0"}
```

`apiVersion` changes only on breaking changes; new routes, fields and commands may be added to
version 1 at any time, so ignore fields you do not know.

### `GET /v1/commands`

Lists every registered command, sorted by id:

```sh
curl -s -H "Authorization: Bearer $TOKEN" "$API/v1/commands"
```

```json
[
    {
        "id": "edit.undo",
        "title": "Undo",
        "description": "Reverts the last change.",
        "category": "Edit",
        "paramsSchema": {"type": "object", "properties": {}, "additionalProperties": false},
        "shortcut": "Ctrl+Z",
        "undoable": false,
        "readOnly": false,
        "confirmation": "",
        "confirmationParam": "",
        "owner": "core",
        "resultSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]},
        "permission": "project.write",
        "startsJob": false,
        "openWorld": false,
        "quiet": false,
        "async": false
    },
    ...
]
```

`paramsSchema` is a JSON Schema (draft 2020-12 subset, see `mikserstudio/core/JsonSchema.h`) of the params
object. `undoable` is true when a successful run adds a step to the undo history. `readOnly` is true
when the command only reads and changes nothing (such as `project.list`). `confirmation`, when not
empty, is the question a client asks the person before it runs the command, because the command
always replaces or deletes their work; the API itself runs it without asking. `confirmationParam`,
when not empty, names a boolean parameter for commands that need the person's agreement only on some
runs: `project.new` and `project.open` (`discard`) only while there are unsaved changes, and
`project.save` and `project.save_as` (`overwrite`) only when they would replace another existing file;
saving to the project's own file never asks. Such a run without the parameter fails with
`409 needs_confirmation`, whose message is the question to ask; the client asks the person and, when
they agree, runs the command again with the parameter set to `true`. A plugin can't agree for the
person: when a plugin runs one of these commands, the run answers `409 needs_confirmation` until the
person agrees.

`owner` is `core` for the app's own commands and the plugin's id for a plugin's; a command's spec
changes (`command.changed`) and its commands go away with their owner. `resultSchema`, when
not empty, is the JSON Schema of a successful run's `result`, such as `{path}` for `project.save`.
`permission` is what a caller needs to run the command ([architecture.md](architecture.md#permissions)):
`project.read` for read-only commands, `project.write` for changes to the project, `files` for
reading and writing files (opening, saving and importing), `network` for talking to the AI tools and
changing their addresses, `ui` for the monitors and playback, and `plugins.manage`; today it is
listed but not enforced. `startsJob` is true when the work goes on in a job that the result names
(`ai.run` answers `{jobId}`; `ai.jobs` follows it). `openWorld` is true when the command reaches
outside the app, to an AI tool that may be on another computer or online. `quiet` is true for
bookkeeping that the command log and the status bar leave out, as they leave out read-only commands:
the seeks of `program.seek` and `source.seek`, which scrubbing sends many times. `async` is true for
a command that answers later, after work that takes a while, such as a plugin's call to a tool;
`POST` waits for it (see below).
Plugins add and remove commands at run time; the events socket reports those changes.

### `GET /v1/commands/<id>`

One command spec, or `404 unknown_command`:

```sh
curl -s -H "Authorization: Bearer $TOKEN" "$API/v1/commands/project.import_media"
```

### `POST /v1/commands/<id>`

Runs a command. The body is the params object; an empty body means `{}`. Defaults from the schema are
filled in before the params are validated.

```sh
curl -s -X POST -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
     -d '{"path": "D:/samples/sample-30s.mp4"}' "$API/v1/commands/project.import_media"
```

```json
{"result": {"mediaId": "media-43f78b2b-a279-419f-9256-c92174caf7aa"}}
```

A command whose spec has `async` answers later; the request waits for it. When it hasn't finished
within 60 seconds, the API stops waiting and answers `409 canceled`: its work may go on, and
`/v1/events` tells when it ends.

Undo and redo are commands too:

```sh
curl -s -X POST -H "Authorization: Bearer $TOKEN" "$API/v1/commands/edit.undo"
# {"result": {"text": "Import \"sample-30s.mp4\""}}
curl -s -X POST -H "Authorization: Bearer $TOKEN" "$API/v1/commands/edit.undo"
# 409 {"error": {"code": "not_available", "message": "There is nothing to undo."}}
```

On Windows `cmd.exe`, quote JSON with double quotes and escape the inner ones:
`-d "{\"path\": \"D:/samples/sample-30s.mp4\"}"`. In PowerShell, use `curl.exe` (not the
`Invoke-WebRequest` alias) or `Invoke-RestMethod`:

```powershell
$api = Get-Content "$env:LOCALAPPDATA\Mikser Studio\api.json" | ConvertFrom-Json
Invoke-RestMethod -Method Post -Uri "$($api.baseUrl)/v1/commands/project.list" `
    -Headers @{ Authorization = "Bearer $($api.token)" } -ContentType 'application/json' -Body '{}'
```

## Events: WebSocket `/v1/events`

A push-only WebSocket on the same port. Authenticate with `Authorization: Bearer <token>` or, for
clients that cannot set headers (browsers), the query `?token=<token>`. A wrong or missing token is
refused with `401` during the handshake.

```sh
curl -s -N "ws://127.0.0.1:52341/v1/events?token=$TOKEN"     # curl 8.11+ with WebSocket support
websocat "ws://127.0.0.1:52341/v1/events?token=$TOKEN"
```

Each text message is one JSON object with a `type`:

```json
{"type": "command.executed", "id": "project.import_media", "params": {"path": "D:/samples/sample-30s.mp4"}, "result": {"mediaId": "media-..."}, "origin": "api", "origins": ["api"]}
{"type": "command.failed", "id": "edit.undo", "params": {}, "error": {"code": "not_available", "message": "There is nothing to undo."}, "origin": "ui", "origins": ["ui"]}
{"type": "command.registered", "id": "hello.greet"}
{"type": "command.unregistered", "id": "hello.greet"}
{"type": "command.changed", "id": "hello.greet"}
{"type": "undo.changed", "canUndo": true, "canRedo": false, "undoText": "Import “sample-30s.mp4”", "redoText": "", "undoOrigins": ["api"]}
{"type": "media.added", "mediaId": "media-...", "origin": "api", "origins": ["api"]}
{"type": "clip.changed", "clipId": "clip-...", "origin": "ui", "origins": ["ui"]}
{"type": "job.progress", "jobId": "aijob-...", "progress": 0.5, "message": "Step 10 of 20", "origin": "api", "origins": ["api"]}
{"type": "transport.state", "transport": "program", "position": 120, "duration": 900, "state": "playing", "frameRate": {"num": 30, "den": 1}}
```

- Events cover **every** command run, whether it came from the API, the UI, a shortcut or a plugin.
  `params` are the params after defaults were applied. `origin` says who started the run: `ui` (the
  person, in the interface), `api` (a client of this API, such as an agent through the MCP bridge) or
  `plugin:<id>`. `origins` is the chain of callers, the one who started the run first: a plugin's
  command run through the API is `["api", "plugin:hello"]`. Commands a command runs keep its origin.
  The project's changes and jobs carry the origin of the command that caused them; `undo.changed`
  names the origins of the step that undo would revert (`undoOrigins`). An event the app causes by
  itself, such as a plugin's commands registered at start-up, has none.
- The project's changes come with the id of what changed: `media.added` and `media.removed`
  (`mediaId`), `clip.added`, `clip.removed` and `clip.changed` (`clipId`), `track.added`,
  `track.removed` and `track.changed` (`trackId`), `project.reset` (a new or opened project) and
  `project.file_changed` (`path`). Read the details with `project.list`.
- Jobs report `job.created` (`jobId`, `kind`, `title`, `owner`, `lane`), `job.progress`
  (`progress` from 0 to 1, `message`; at most 10 a second per job) and `job.finished` (`state`:
  `succeeded`, `failed` or `canceled`, `error`, `result`). `jobs.list` lists them, the AI engine's
  included, with the same ids as `ai.run` gives; `jobs.cancel {jobId}` cancels one.
- `transport.state` reports a monitor's playback (`transport` is `program` or `source`, with the
  status the transport commands return), when it changes and at most 10 times a second per monitor:
  the first change at once and the last one of a burst within 100 ms.
- For an undoable command, `undo.changed` arrives before its `command.executed`.
- No state is sent on connect: call `GET /v1/commands` (and the commands you care about) after the
  socket is open to get the current state, then apply events.
- Messages sent by the client are ignored (incoming messages are limited to 4 KiB). A client that
  stops reading and lets more than 8 MiB of events pile up is disconnected.
- When the server stops, clients are closed with code 1001 (going away).

## MCP bridge

The MCP bridge offers the commands to agents that speak the
[Model Context Protocol](https://modelcontextprotocol.io), such as Claude Code. It is a program of
its own, `mikser-studio-mcp` (`src/mcp`), built and shipped next to `mikser-studio`, that the agent's
client starts and talks to on stdin and stdout, one JSON-RPC message a line. It speaks the protocol
versions 2025-11-25, 2025-06-18, 2025-03-26 and 2024-11-05. It uses only what this page describes: the
discovery file, `/v1/commands` and `/v1/events`.

- **Tools:** every command is a tool, named after its id with `_` for `.` (`project.import_media`
  is `project_import_media`), with its title, description and `paramsSchema` as the input schema.
  The app refuses a command whose tool name would be longer than MCP's 64 characters, would be
  `recent_events` (the bridge's own tool), or would equal another command's.
  Read-only commands carry `readOnlyHint`, and commands with a `confirmation` or a
  `confirmationParam` carry `destructiveHint`; a `confirmationParam` is left out of the input schema,
  and the description says that the person is asked first. Commands with `openWorld` carry
  `openWorldHint`, and a command's `resultSchema` is the tool's output schema for clients of protocol
  version 2025-06-18 or later. A tool's result is the command's
  `result`, as JSON text and, for clients of protocol version 2025-06-18 or later, as structured
  content; a command's error is a tool error with its message and code.
- **Confirmations:** before a command with a `confirmation`, the bridge asks the person through the
  client (MCP elicitation) and runs the command only when they accept. When a run answers
  `needs_confirmation`, the bridge asks with its message and, when they accept, runs it again with
  the `confirmationParam` set; an agent's own value for that parameter is dropped. A client that
  cannot ask gets a tool error, and the command does not run.
- **Following the app:** the bridge reads the discovery file anew for every request, so it follows the
  app when it restarts. While the app is not running it lists no tools, and calls say to start it. It
  keeps the events socket open and tells the client when the tools change: when the app starts or
  stops, and when plugins add, change or remove commands. Changes that come within 50 ms of each
  other become one `notifications/tools/list_changed`.
- **Recent events:** the bridge's own read-only tool `recent_events` lists the last 100 events it
  received (except `transport.state`, which playback sends often: read it with `program.status`),
  newest last, each with a `number`, the `time` it arrived and the `event` as the socket sent
  it, so an agent sees what the person changed. Its result's `latest` is the newest number; pass it as
  `after` the next time to get only the events since. The tool is listed with the commands, and a
  command whose tool would take its name is left out.
- **Which app:** `--instance` chooses the installed app (`installed`, the default unless
  `MIKSERSTUDIO_INSTANCE` names another), `dev` (a build from source) or `e2e`; `--discovery-file`
  names another `api.json`. Its log goes to stderr.

To connect Claude Code to Mikser Studio Dev, from the repository after a `debug` build:

```sh
claude mcp add mikser-studio -e 'PATH=C:\msys64\ucrt64\bin' -- "$PWD/build/debug/bin/mikser-studio-mcp" --instance dev
```

A build from source needs MSYS2's libraries (Qt and the C++ run time), which are not on the `PATH`
Claude Code starts programs with, so the command gives the bridge a `PATH` of MSYS2's UCRT64 `bin`
folder, as `start.bat` does for the app (with MSYS2 elsewhere, its `ucrt64\bin`; on Linux and macOS
leave `-e` out). Without it the bridge does not start, and Claude Code shows it as failed.

To connect it to the installed Mikser Studio, give the program in the installation's `bin` folder,
for one user by default `%LOCALAPPDATA%\Programs\Mikser Studio\bin\mikser-studio-mcp.exe`:

```sh
claude mcp add mikser-studio -- "$LOCALAPPDATA/Programs/Mikser Studio/bin/mikser-studio-mcp.exe"
```

Other clients take the same program and arguments in their MCP settings.

## Embedding (C++)

```cpp
const mikserstudio::core::UserFolders folders = mikserstudio::core::UserFolders::resolve();
mikserstudio::api::ApiServer api(bus, {.port = 0, .token = {}, .discoveryFile = folders.discoveryFile});
if (auto port = api.start(); !port)
    qCWarning(lcApp) << port.error();
```

`ApiServer` must be created and started on the thread that owns the `CommandBus` (the main thread),
and destroyed before the bus. `start()` returns the bound port or an English error (no discovery file given, port taken,
discovery file not writable, malformed fixed token). `stop()` is idempotent and is called by the
destructor.
