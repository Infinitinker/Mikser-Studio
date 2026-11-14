-- The Hello example plugin: the command "mikserstudio.examples.hello.greet" {name} -> {message},
-- whose message starts with the "greeting" setting, and the "Greeter" panel (hello.qml) that runs
-- it. Copy this folder to start a plugin of your own (docs/plugins.md).

ms.command({
    id = "mikserstudio.examples.hello.greet",
    title = "Say hello",
    description = "Greets someone with the greeting from the plugin settings.",
    category = "Examples",
    read_only = true,
    params = {
        type = "object",
        properties = {
            name = { type = "string", default = "world", description = "Who to greet." },
        },
        additionalProperties = false,
    },
    result = {
        type = "object",
        properties = { message = { type = "string" } },
        required = { "message" },
    },
    handler = function(params)
        return { message = ("%s, %s!"):format(ms.settings().greeting, params.name) }
    end,
})

ms.log.info("Hello plugin is ready.")
