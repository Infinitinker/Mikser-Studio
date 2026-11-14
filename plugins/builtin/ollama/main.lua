-- The Ollama plugin: "generate_text" answers a prompt with a language model that Ollama serves, as a
-- job whose result is {text, model, provenance}, and "models" lists the installed models. The
-- address of Ollama is the "url" setting; a job uses the one set when it was asked for.

local ollama = require("ollama")

local MODEL_SCHEMA = {
    type = "object",
    properties = {
        name = { type = "string" },
        digest = { type = "string" },
        family = { type = "string" },
        parameterSize = { type = "string" },
        quantization = { type = "string" },
    },
}

-- At most `limit` characters of `text`, on one line.
local function shortened(text, limit)
    local line = text:gsub("%s+", " ")
    if utf8.len(line) and utf8.len(line) > limit then
        return line:sub(1, utf8.offset(line, limit + 1) - 1) .. "…"
    end
    return line
end

ms.command({
    id = "mikserstudio.ollama.generate_text",
    title = "Generate text",
    description = "Answers a prompt with a language model that Ollama serves. The work goes on in a job, "
        .. "whose result is {text, model, provenance}.",
    category = "AI",
    permission = "network",
    open_world = true,
    starts_job = true,
    params = {
        type = "object",
        properties = {
            prompt = { type = "string", minLength = 1, description = "What to ask the model." },
            model = {
                type = "string",
                minLength = 1,
                description = "Ollama model name; the first installed model that can write when omitted.",
            },
        },
        required = { "prompt" },
        additionalProperties = false,
    },
    result = {
        type = "object",
        properties = { jobId = { type = "string" } },
        required = { "jobId" },
    },
    handler = function(params)
        local base = ms.settings().url
        local started = ms.jobs.run({
            kind = "generate_text",
            title = "Generate text: " .. shortened(params.prompt, 60),
            lane = "generate",
        }, function(job)
            return ollama.generate(base, params, job)
        end)
        return { jobId = started.id }
    end,
})

ms.command({
    id = "mikserstudio.ollama.models",
    title = "List Ollama models",
    description = "Lists the models installed in Ollama, with their family, size and quantization.",
    category = "AI",
    permission = "network",
    read_only = true,
    open_world = true,
    result = {
        type = "object",
        properties = { models = { type = "array", items = MODEL_SCHEMA } },
        required = { "models" },
    },
    handler = function()
        local models = ms.array({})
        for _, model in ipairs(ollama.models(ms.settings().url)) do
            table.insert(models, ollama.describe(model))
        end
        return { models = models }
    end,
})
