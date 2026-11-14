# Writing a native plugin

A native plugin is a shared library plus a `mikserstudio-plugin.json` manifest in its own folder. It can
add commands (usable from menus, the command palette, macros, the `/v1` API and other plugins),
settings (the UI generates the form from a JSON Schema) and QML panels. A complete example is
`plugins/hello/`.

## Layout and discovery

```
<plugins folder>/
  hello/
    mikserstudio-plugin.json
    hello.dll | libhello.so | hello.so | libhello.dylib
    hello.qml
```

Mikser Studio looks at every subfolder of each plugin folder (`bin/plugins/` next to the executable for
bundled plugins) that contains a `mikserstudio-plugin.json`. Discovery only reads manifests; code is
loaded when the plugin is activated. A plugin that fails (bad manifest, missing library, error in
`activate`) is listed as *Failed* with a message and never takes the app down. A folder whose
manifest is invalid is listed under its folder name, so it can never hide a valid plugin; when two
valid manifests share an id, the first folder in name order wins.

## Manifest

```json
{
  "id": "mikserstudio.examples.hello",
  "name": "Hello",
  "version": "0.1.0",
  "description": "Example native plugin.",
  "apiVersion": 1,
  "kind": "native",
  "entry": "hello",
  "capabilities": ["commands", "panels"],
  "settingsSchema": {
    "type": "object",
    "properties": { "greeting": { "type": "string", "default": "Hello" } },
    "additionalProperties": false
  },
  "panels": [
    { "id": "greeter", "title": "Greeter", "qml": "hello.qml", "defaultArea": "right" }
  ]
}
```

| Field | Rules |
|---|---|
| `id` | Dotted lower_snake, at least two parts (`^[a-z0-9_]+(\.[a-z0-9_]+)+$`). Prefix of all your command ids. |
| `name`, `version` | Display name; SemVer version. |
| `apiVersion` | Must be `1`. |
| `kind` | `"native"`, the one kind. Mikser Studio no longer runs Python plugins: a manifest with `"python"` is listed as *Failed*, with a message that says so. |
| `entry` | The library file name without folder, `lib` prefix or extension. |
| `settingsSchema` | Optional JSON Schema with `"type": "object"`; see Settings. |
| `panels` | Optional. `qml` is a path inside the plugin folder; `defaultArea` is `left`, `right` (default), `bottom`, `center` or `floating`. Each panel is registered as `<plugin-id>/<panel-id>`. |

## The C ABI

Include `sdk/include/mikserstudio/plugin_api.h` (MIT, plain C) and export one function:

```c
MIKSERSTUDIO_PLUGIN_EXPORT const MikserStudioPluginV1* mikserstudio_plugin_v1(void);
```

It returns a static table `{struct_size, api_version = MIKSERSTUDIO_PLUGIN_API_VERSION, activate, deactivate}`.

- `activate(host, &state, error)` runs once on the main thread. Keep `host` (valid until
  `deactivate` returns), register your commands and return 0. On failure write a message to
  `error` and return non-zero: the host unregisters anything you registered and unloads the
  library without calling `deactivate`, so free your own state before returning.
- `deactivate(state)` runs once before unloading, and only if `activate` returned 0; your commands
  are already unregistered.
- Strings are UTF-8 with explicit lengths and valid only during the call. Never let C++ exceptions
  or other language-specific types cross the boundary.

Host functions (always pass `host->host` as the first argument):

| Function | Use |
|---|---|
| `log(level, message)` | Writes to the `mikserstudio.core.plugins` log as `[plugin-id] message`. |
| `register_command(spec_json, fn, user_data, error)` | `spec_json` = `{"id", "title", "description", "category", "paramsSchema", "resultSchema", "shortcut", "readOnly", "quiet", "openWorld", "confirmation"}`; only `id` and `title` are required. The id must start with `<plugin-id>.`; `category` defaults to the plugin name. Set `"readOnly": true` for commands that never change anything (UIs keep them out of logs, API clients may cache them), `"quiet": true` for bookkeeping that changes something but is not worth a log line, and `"openWorld": true` for commands that reach outside the app, such as an online service. `resultSchema` is the JSON Schema (`"type": "object"`) of the result. The command's owner is the plugin's id. A non-empty `confirmation` is an English question the UI asks the user before running the command; the API does not ask. Plugin commands are not undoable in API 1. |
| `execute_command(id, params_json, result, error)` | Runs any command like the UI would. Returns 0 on success. |
| `get_settings(sink)` | Current settings as a JSON object, schema defaults applied. |

A command function receives the params already validated against its `paramsSchema` with
defaults filled in. Write a JSON object (or nothing, meaning `{}`) to `result` and return 0, or
write an English message for the user to `error` and return non-zero.

The supported JSON Schema subset is listed in `src/core/include/mikserstudio/core/JsonSchema.h`
(`type`, `enum`, `const`, numeric bounds, string lengths, `pattern`, `properties`, `required`,
`additionalProperties`, `items`, `minItems`, `maxItems`, `default`). An empty `paramsSchema`
accepts only `{}`.

## Settings

Settings are a JSON object validated against `settingsSchema`, with `default` values filled in.
They are stored as `<settings folder>/<plugin-id>.json`; invalid stored settings are ignored in
favour of the defaults. Read them with `get_settings` whenever you need them: users can change them
while the plugin is active.

## Panels (QML contract)

The UI loads the panel's QML file and gives its root item one context property, `mikserstudio`:

```qml
mikserstudio.execute(commandId: string, params: object) -> object
    // {ok: true, result: {...}}  or  {ok: false, error: {code, message}}
    // code is "unknown_command", "invalid_params", "failed" or "not_available"
mikserstudio.settings   // object: the plugin's current settings
mikserstudio.pluginId   // string
```

Use only this contract and the standard `QtQuick`, `QtQuick.Controls` and `QtQuick.Layouts`
modules, so panels keep working when the host's Qt or C++ side changes. Every action of a panel
should go through `mikserstudio.execute`, which gives it validation, undo (for core commands) and API
access for free. See `plugins/hello/hello.qml`.

## Building

A bundled plugin is a folder in `plugins/` with a `CMakeLists.txt`; `plugins/CMakeLists.txt` builds every
such folder, so adding one changes nothing outside it. `plugins/hello/CMakeLists.txt` takes every name
from the folder's name, so a copy of the folder builds as a plugin of its own: the library
`<folder>.dll` (the manifest's `entry`), from every `.cpp` in `src/`, built into `bin/plugins/<folder>/`
with the manifest and every `.qml` file next to it, and installed there. CONTRIBUTING.md walks through
making a plugin from it.

The plugin does not need Qt; the hello example has no dependencies beyond the C++ runtime.
