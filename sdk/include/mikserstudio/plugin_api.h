/*
 * Mikser Studio native plugin ABI, version 1.
 *
 * SPDX-License-Identifier: MIT
 *
 * A native plugin is a shared library next to a mikserstudio-plugin.json manifest whose "kind" is
 * "native" and whose "entry" names the library file without its extension. It exports one C
 * function, mikserstudio_plugin_v1(), returning a static MikserStudioPluginV1 table.
 *
 * Rules of the ABI:
 *   - Plain C, no C++ types or exceptions cross the boundary.
 *   - Strings are UTF-8 with an explicit length; they are valid only during the call.
 *   - Every struct starts with struct_size so later versions can append fields.
 *   - The host calls into the plugin only on its main thread.
 */
#ifndef MIKSERSTUDIO_PLUGIN_API_H
#define MIKSERSTUDIO_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIKSERSTUDIO_PLUGIN_API_VERSION 1u

#if defined(_WIN32)
#define MIKSERSTUDIO_PLUGIN_EXPORT __declspec(dllexport)
#else
#define MIKSERSTUDIO_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum MikserStudioLogLevel {
    MIKSERSTUDIO_LOG_DEBUG = 0,
    MIKSERSTUDIO_LOG_INFO = 1,
    MIKSERSTUDIO_LOG_WARNING = 2,
    MIKSERSTUDIO_LOG_ERROR = 3
} MikserStudioLogLevel;

/* Receives a string from the other side; may be called at most once per sink. */
typedef struct MikserStudioStringSink {
    void* context;
    void (*write)(void* context, const char* utf8, size_t length);
} MikserStudioStringSink;

/*
 * Runs a plugin command. params_json is the validated params object.
 * On success write a JSON object (or nothing, meaning {}) to `result` and return 0.
 * On failure write an English message for the user to `error` and return non-zero.
 */
typedef int (*MikserStudioCommandFn)(void* user_data, const char* params_json, size_t params_length,
                                     const MikserStudioStringSink* result, const MikserStudioStringSink* error);

typedef struct MikserStudioHostApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* host; /* Opaque; pass back as the first argument of every function below. */

    void (*log)(void* host, MikserStudioLogLevel level, const char* message, size_t length);

    /*
     * Registers a command. spec_json is a CommandSpec object:
     *   {"id", "title", "description", "category", "paramsSchema", "shortcut", "readOnly",
     *    "confirmation"}
     * Only "id" and "title" are required. "readOnly": true marks a command that never changes
     * state. A non-empty "confirmation" is an English question the UI asks before running the
     * command (the API does not ask). The id must start with the plugin id followed by a dot.
     * Plugin commands are not undoable in API version 1. Returns 0 on success; on failure writes
     * the reason to `error`.
     */
    int (*register_command)(void* host, const char* spec_json, size_t spec_length, MikserStudioCommandFn fn,
                            void* user_data, const MikserStudioStringSink* error);

    /* Executes any command by id, like the UI or API would. Returns 0 on success. */
    int (*execute_command)(void* host, const char* id, size_t id_length, const char* params_json, size_t params_length,
                           const MikserStudioStringSink* result, const MikserStudioStringSink* error);

    /* Current plugin settings as a JSON object, validated against the manifest schema. */
    void (*get_settings)(void* host, const MikserStudioStringSink* settings_json);
} MikserStudioHostApiV1;

typedef struct MikserStudioPluginV1 {
    uint32_t struct_size;
    uint32_t api_version; /* Must be MIKSERSTUDIO_PLUGIN_API_VERSION the plugin was built against. */

    /*
     * Called once after loading. Store `host` (it stays valid until deactivate returns) and
     * register commands. Return 0 on success; non-zero leaves the plugin inactive and the host
     * shows the message written to `error`. After a failed activate the host unregisters the
     * commands registered so far and unloads the library WITHOUT calling deactivate, so free any
     * state you allocated before returning non-zero.
     */
    int (*activate)(const MikserStudioHostApiV1* host, void** plugin_state, const MikserStudioStringSink* error);

    /*
     * Called once before unloading, only after a successful activate; the plugin's commands are
     * already unregistered.
     */
    void (*deactivate)(void* plugin_state);
} MikserStudioPluginV1;

typedef const MikserStudioPluginV1* (*MikserStudioPluginEntryFn)(void);

#define MIKSERSTUDIO_PLUGIN_ENTRY_SYMBOL "mikserstudio_plugin_v1"

#ifdef __cplusplus
}
#endif

#endif /* MIKSERSTUDIO_PLUGIN_API_H */
