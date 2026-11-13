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
| 404 | `unknown_command` | No command with that id. |
| 404 | `not_found` | No such route. |
| 405 | `method_not_allowed` | Wrong HTTP method (the `Allow` header lists the right ones). |
| 409 | `not_available` | The command exists but cannot run now (e.g. nothing to undo). |
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
        "confirmation": ""
    },
    ...
]
```

`paramsSchema` is a JSON Schema (draft 2020-12 subset, see `mikserstudio/core/JsonSchema.h`) of the params
object. `undoable` is true when a successful run adds a step to the undo history. `readOnly` is true
when the command only reads and changes nothing (such as `project.list`). `confirmation`, when not
empty, is the question a client asks the person before it runs the command, because the command
replaces or deletes their work (such as `project.new` and `project.open`); the API itself runs it
without asking. Plugins add and remove commands at run time; the events socket reports those changes.

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
{"type": "command.executed", "id": "project.import_media", "params": {"path": "D:/samples/sample-30s.mp4"}, "result": {"mediaId": "media-..."}}
{"type": "command.failed", "id": "edit.undo", "params": {}, "error": {"code": "not_available", "message": "There is nothing to undo."}}
{"type": "command.registered", "id": "hello.greet"}
{"type": "command.unregistered", "id": "hello.greet"}
{"type": "undo.changed", "canUndo": true, "canRedo": false, "undoText": "Import “sample-30s.mp4”", "redoText": ""}
```

- Events cover **every** command run, whether it came from the API, the UI, a shortcut or a plugin.
  `params` are the params after defaults were applied.
- For an undoable command, `undo.changed` arrives before its `command.executed`.
- No state is sent on connect: call `GET /v1/commands` (and the commands you care about) after the
  socket is open to get the current state, then apply events.
- Messages sent by the client are ignored (incoming messages are limited to 4 KiB). A client that
  stops reading and lets more than 8 MiB of events pile up is disconnected.
- When the server stops, clients are closed with code 1001 (going away).

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
