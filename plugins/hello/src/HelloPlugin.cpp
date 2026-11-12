// The "Hello" example plugin. It registers "mikserstudio.examples.hello.greet" {name} -> {message},
// where the message starts with the "greeting" setting. See docs/plugins.md.

#include "FlatJson.h"

#include <mikserstudio/plugin_api.h>

#include <exception>
#include <new>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kGreetSpec = R"json({
    "id": "mikserstudio.examples.hello.greet",
    "title": "Say hello",
    "description": "Greets someone with the greeting from the plugin settings.",
    "category": "Examples",
    "readOnly": true,
    "paramsSchema": {
        "type": "object",
        "properties": {
            "name": {"type": "string", "default": "world", "description": "Who to greet."}
        },
        "additionalProperties": false
    }
})json";

struct HelloState {
    const MikserStudioHostApiV1* host = nullptr;
};

void write(const MikserStudioStringSink* sink, std::string_view text)
{
    if (sink && sink->write)
        sink->write(sink->context, text.data(), text.size());
}

void log(const MikserStudioHostApiV1* host, MikserStudioLogLevel level, std::string_view text)
{
    host->log(host->host, level, text.data(), text.size());
}

void collect(void* context, const char* utf8, size_t length)
{
    static_cast<std::string*>(context)->append(utf8, length);
}

std::string greeting(const MikserStudioHostApiV1* host)
{
    std::string json;
    const MikserStudioStringSink sink{&json, &collect};
    host->get_settings(host->host, &sink);
    const auto settings = hello::stringMembers(json);
    if (settings) {
        if (const auto it = settings->find("greeting"); it != settings->end())
            return it->second;
    }
    return "Hello";
}

int greet(void* userData, const char* paramsJson, size_t paramsLength, const MikserStudioStringSink* result,
          const MikserStudioStringSink* error)
{
    try {
        const auto* state = static_cast<const HelloState*>(userData);
        const auto params = hello::stringMembers(std::string_view(paramsJson, paramsLength));
        if (!params) {
            write(error, "The greet parameters are not a JSON object.");
            return 1;
        }
        const auto name = params->find("name");
        const std::string message = greeting(state->host) + ", " + (name == params->end() ? "world" : name->second) +
                                    "!";
        write(result, "{\"message\":" + hello::quoted(message) + "}");
        return 0;
    } catch (const std::exception& exception) {
        write(error, exception.what());
        return 1;
    }
}

int activate(const MikserStudioHostApiV1* host, void** pluginState, const MikserStudioStringSink* error)
{
    if (!host || host->struct_size < sizeof(MikserStudioHostApiV1) ||
        host->api_version != MIKSERSTUDIO_PLUGIN_API_VERSION) {
        write(error, "This version of Mikser Studio does not provide plugin API version 1.");
        return 1;
    }
    auto* state = new (std::nothrow) HelloState{host};
    if (!state) {
        write(error, "Out of memory.");
        return 1;
    }
    if (host->register_command(host->host, kGreetSpec.data(), kGreetSpec.size(), &greet, state, error) != 0) {
        delete state;
        return 1;
    }
    *pluginState = state;
    log(host, MIKSERSTUDIO_LOG_INFO, "Hello plugin is ready.");
    return 0;
}

void deactivate(void* pluginState)
{
    delete static_cast<HelloState*>(pluginState);
}

constexpr MikserStudioPluginV1 kPlugin{
    sizeof(MikserStudioPluginV1),
    MIKSERSTUDIO_PLUGIN_API_VERSION,
    &activate,
    &deactivate,
};

}  // namespace

extern "C" MIKSERSTUDIO_PLUGIN_EXPORT const MikserStudioPluginV1* mikserstudio_plugin_v1(void)
{
    return &kPlugin;
}
