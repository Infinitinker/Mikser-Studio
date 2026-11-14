-- Talking to Ollama: its version and models, choosing a model that can write, streaming its answer
-- and turning what Ollama says into results and messages for the person.

local M = {}

local QUICK = 3 -- seconds: a missing Ollama is noticed quickly
local SHOW = 15 -- /api/show reads the model file's metadata, which can take a moment on a cold disk
local SILENCE = 600 -- loading a large model before the first token can take minutes

local NO_MODELS = 'Ollama has no models. Download one with "ollama pull <model>" and try again.'
local RAN_OUT = "The model ran out of room before it answered; allow it more tokens or use a model that doesn't "
    .. "think first."

local function trim(text)
    return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function endpoint(base, path)
    return (base:gsub("/+$", "")) .. path
end

-- Raises `problem` again when it is a cancel, so a canceled job ends as canceled.
local function rethrow_cancel(problem)
    if type(problem) == "table" and problem.code == "canceled" then
        error(problem, 0)
    end
end

-- A job's error as a clause for another sentence: its words without the closing period.
local function message_of(problem)
    local message = type(problem) == "table" and problem.message or tostring(problem)
    return (message:gsub("%.$", ""))
end

-- The answer to a GET or POST of Ollama's API, or an error that says Ollama isn't reachable.
local function call(base, path, body, timeout)
    local ok, answer = pcall(function()
        return ms.jobs
            .start("http.request", {
                url = endpoint(base, path),
                method = body and "POST" or "GET",
                body = body,
                timeout = timeout,
                title = "Ollama: " .. path,
            })
            :await()
    end)
    if not ok then
        rethrow_cancel(answer)
        error(("Ollama is not reachable at %s (%s). Start Ollama and try again."):format(base, message_of(answer)), 0)
    end
    return answer
end

local function call_json(base, path, body, timeout)
    local answer = call(base, path, body, timeout)
    if answer.status ~= 200 or answer.json == nil then
        local reason = answer.json and answer.json.error or ("HTTP %d"):format(answer.status)
        error(("Ollama is not reachable at %s (%s). Start Ollama and try again."):format(base, reason), 0)
    end
    return answer.json
end

local function parse_models(tags)
    local models = {}
    for _, model in ipairs(tags.models or {}) do
        if type(model.name) == "string" and model.name ~= "" then
            table.insert(models, { name = model.name, digest = model.digest or "", details = model.details or {} })
        end
    end
    return models
end

-- What provenance and the models command say about a model.
function M.describe(model)
    local described = { name = model.name, digest = model.digest }
    local keys = { family = "family", parameter_size = "parameterSize", quantization_level = "quantization" }
    for key, name in pairs(keys) do
        local value = model.details[key]
        if value ~= nil and value ~= "" then
            described[name] = tostring(value)
        end
    end
    return described
end

-- The installed models.
function M.models(base)
    call_json(base, "/api/version", nil, QUICK)
    return parse_models(call_json(base, "/api/tags", nil, QUICK))
end

-- The text after the model's last </think>, without the space around it, or nil when the answer
-- stops inside <think>: the model ran out of room while it reasoned.
function M.strip_reasoning(text)
    local last_open, last_close
    local from = 1
    while true do
        local first, last = text:find("<think>", from, true)
        if not first then
            break
        end
        last_open, from = first, last + 1
    end
    from = 1
    while true do
        local first, last = text:find("</think>", from, true)
        if not first then
            break
        end
        last_close, from = last, last + 1
    end
    if last_open and (not last_close or last_close < last_open) then
        return nil
    end
    return trim(last_close and text:sub(last_close + 1) or text)
end

local function names_of(models)
    local names = {}
    for _, model in ipairs(models) do
        table.insert(names, model.name)
    end
    return table.concat(names, ", ")
end

local function can_complete(capabilities)
    -- An Ollama that doesn't say what a model can do is trusted to generate text.
    if type(capabilities) ~= "table" then
        return true
    end
    for _, capability in ipairs(capabilities) do
        if capability == "completion" then
            return true
        end
    end
    return false
end

local function capabilities_of(base, model)
    local ok, shown = pcall(call_json, base, "/api/show", { model = model.name }, SHOW)
    if not ok then
        rethrow_cancel(shown)
        error(
            ("Ollama did not describe the model %s (%s). Start Ollama and try again."):format(
                model.name,
                message_of(shown)
            ),
            0
        )
    end
    return shown.capabilities
end

-- The model to write with: the one asked for, or the first installed one that can.
local function choose(base, models, wanted)
    if #models == 0 then
        error(NO_MODELS, 0)
    end
    if not wanted then
        for _, model in ipairs(models) do
            if can_complete(capabilities_of(base, model)) then
                return model
            end
        end
        error(
            'None of the installed Ollama models can generate text. Download one with "ollama pull <model>" (for '
                .. "example a Llama, Qwen or Gemma model) and try again.",
            0
        )
    end
    for _, model in ipairs(models) do
        if model.name == wanted or model.name == wanted .. ":latest" then
            local capabilities = capabilities_of(base, model)
            if can_complete(capabilities) then
                return model
            end
            local offers = type(capabilities) == "table" and #capabilities > 0 and table.concat(capabilities, ", ")
                or "nothing"
            error(
                ("The Ollama model “%s” cannot generate text (it offers: %s). Choose another model."):format(
                    model.name,
                    offers
                ),
                0
            )
        end
    end
    error(
        (
            'The model “%s” is not installed in Ollama (installed: %s). Download it with "ollama pull %s" or choose '
            .. "an installed one."
        ):format(wanted, names_of(models), wanted),
        0
    )
end

-- Answers `params.prompt` with a model of the Ollama at `base`, reporting progress on `job`;
-- returns {text, model, provenance}.
function M.generate(base, params, job)
    local version = call_json(base, "/api/version", nil, QUICK).version or "unknown"
    local model = choose(base, parse_models(call_json(base, "/api/tags", nil, QUICK)), params.model)
    job:progress(0, "Loading " .. model.name)
    local ok, answer = pcall(function()
        return ms.jobs
            .start("http.stream_ndjson", {
                url = endpoint(base, "/api/generate"),
                body = { model = model.name, prompt = params.prompt, stream = true },
                fields = { "response", "thinking" },
                messages = {
                    response = "Writing with " .. model.name .. " ({count} tokens)",
                    thinking = "Thinking with " .. model.name .. " ({count} tokens)",
                },
                timeout = SILENCE,
                title = "Ollama: /api/generate",
            })
            :await()
    end)
    if not ok then
        rethrow_cancel(answer)
        ms.log.warning(("Ollama failed while answering with %s - %s"):format(model.name, message_of(answer)))
        error(
            ("Ollama stopped answering with %s. Check that Ollama is still running and that the model works in "):format(
                model.name
            ) .. "Ollama itself, then try again; Mikser Studio’s log has Ollama’s message.",
            0
        )
    end
    local text = M.strip_reasoning(answer.fields.response)
    local cut_off = answer.last.done_reason == "length"
    if text == nil or (cut_off and text == "" and answer.fields.thinking ~= "") then
        error(RAN_OUT, 0)
    end
    return {
        text = text,
        model = model.name,
        provenance = {
            plugin = ms.plugin_id,
            operation = "generate_text",
            params = params,
            model = M.describe(model),
            tools = { ollama = version },
            createdAt = os.date("!%Y-%m-%dT%H:%M:%SZ"),
        },
    }
end

return M
