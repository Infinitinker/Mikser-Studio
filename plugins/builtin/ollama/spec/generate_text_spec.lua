-- generate_text against the fake Ollama: the first model that can write answers without its
-- reasoning and with provenance; the model asked for; Ollama's errors; an answer cut off while the
-- model thinks; no models; no Ollama; and a new address for new jobs only.

local plugin = "mikserstudio.ollama"
local fake = ms.settings().fakes.ollama
local RAN_OUT = "The model ran out of room before it answered; allow it more tokens or use a model that doesn't "
    .. "think first."

local function equal(actual, expected)
    if actual ~= expected then
        error(("expected %s, got %s"):format(tostring(expected), tostring(actual)), 2)
    end
end

local function contains(text, part)
    if type(text) ~= "string" or not text:find(part, 1, true) then
        error(("expected %q in %s"):format(part, tostring(text)), 2)
    end
end

-- Sets up the fake Ollama: its models ({name, capabilities}) and the lines of its answer.
local function setup(config)
    ms.jobs.start("http.request", { url = fake .. "/fake/setup", method = "POST", body = config }):await()
end

-- The bodies of the /api/generate requests the fake Ollama got.
local function generate_requests()
    local answer = ms.jobs.start("http.request", { url = fake .. "/fake/requests" }):await()
    local bodies = {}
    for _, request in ipairs(answer.json.requests) do
        if request.path == "/api/generate" then
            table.insert(bodies, request.body)
        end
    end
    return bodies
end

local function use(url)
    ms.run("plugins.set_settings", { id = plugin, settings = { url = url } })
end

-- Fails the spec with the job's error when it didn't succeed.
local function succeeded(ok, result)
    if not ok then
        error("the job failed: " .. tostring(type(result) == "table" and result.message or result), 2)
    end
end

-- Runs generate_text and waits for its job: true and the result, or false and the error.
local function generate(params)
    local job = ms.jobs.get(ms.run(plugin .. ".generate_text", params).jobId)
    return pcall(job.await, job)
end

use(fake)

local ok, result = generate({ prompt = "Say hello" })
succeeded(ok, result)
equal(result.text, "Hello there.")
equal(result.model, "llama3:latest")
local provenance = result.provenance
equal(provenance.plugin, plugin)
equal(provenance.operation, "generate_text")
equal(provenance.params.prompt, "Say hello")
equal(provenance.tools.ollama, "0.12.3")
equal(provenance.model.name, "llama3:latest")
equal(provenance.model.digest, "sha256:llama3:latest")
equal(provenance.model.family, "llama")
equal(provenance.model.parameterSize, "8.0B")
equal(provenance.model.quantization, "Q4_K_M")
contains(provenance.createdAt, "T")
local requests = generate_requests()
equal(#requests, 1)
equal(requests[1].model, "llama3:latest")
equal(requests[1].stream, true)

ok, result = generate({ prompt = "Hi", model = "llama3" })
succeeded(ok, result)
equal(result.model, "llama3:latest")

ok, result = generate({ prompt = "Hi", model = "nope" })
equal(ok, false)
contains(result.message, "The model “nope” is not installed in Ollama (installed: vision-only:latest, llama3:latest)")

ok, result = generate({ prompt = "Hi", model = "vision-only" })
equal(ok, false)
contains(result.message, "cannot generate text (it offers: vision)")
equal(#generate_requests(), 2)

setup({
    answer = { '{"response":"Hel","done":false}', '{"error":"model runner has unexpectedly stopped"}' },
})
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
contains(result.message, "Ollama stopped answering with llama3:latest.")

-- Part 4 item 4: an answer that ends inside <think>, or with only thinking, ran out of room.
setup({
    answer = {
        '{"response":"<think>Let me","done":false}',
        '{"response":" see","done":true,"done_reason":"length"}',
    },
})
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
equal(result.message, RAN_OUT)

setup({ answer = { '{"thinking":"Let me see","response":"","done":true,"done_reason":"length"}' } })
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
equal(result.message, RAN_OUT)

setup({ models = ms.array({}) })
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
contains(result.message, "Ollama has no models.")

-- A new address applies to the jobs asked for after it; one asked for before keeps its own.
setup({ models = { { name = "llama3:latest", capabilities = { "completion" } } }, answer = { '{"response":"Yes"}' } })
local before = ms.jobs.get(ms.run(plugin .. ".generate_text", { prompt = "First" }).jobId)
use("http://127.0.0.1:9")
ok, result = generate({ prompt = "Second" })
equal(ok, false)
contains(result.message, "Ollama is not reachable at http://127.0.0.1:9")
equal(before:await().text, "Yes")
