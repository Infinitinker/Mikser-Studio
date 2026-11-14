-- generate_image against the fake ComfyUI: the image comes into the project with its provenance, the
-- workflow goes into the instance's own folder with the seed; what ComfyUI lacks or refuses, a ComfyUI
-- that isn't running, canceling, and a new address for new jobs only.

local plugin = "mikserstudio.comfyui"
local fake = ms.settings().fakes.comfyui

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

local function control(path, body)
    local answer = ms.jobs.start("http.request", {
        url = fake .. path,
        method = body and "POST" or "GET",
        body = body,
    }):await()
    return answer.json
end

-- Sets up the fake ComfyUI: {missingModels, missingNodes, refusePrompts, holdPrompts, imageCount}.
local function setup(config)
    control("/fake/setup", config)
end

-- The prompts the fake ComfyUI was given, {id, workflow}, and those it was asked to delete.
local function prompts()
    local answer = control("/fake/prompts")
    return answer.prompts, answer.deleted
end

-- Waits, a request at a time, until the fake ComfyUI has `count` prompts.
local function wait_for_prompts(count)
    for _ = 1, 500 do
        local list = prompts()
        if #list >= count then
            return list
        end
    end
    error(("ComfyUI never got %d prompts"):format(count), 2)
end

local function use(url)
    ms.run("plugins.set_settings", { id = plugin, settings = { url = url } })
end

local function start(params)
    return ms.jobs.get(ms.run(plugin .. ".generate_image", params).jobId)
end

-- Runs generate_image and waits for its job: true and the result, or false and the error.
local function generate(params)
    local job = start(params)
    return pcall(job.await, job)
end

local function succeeded(ok, result)
    if not ok then
        error("the job failed: " .. tostring(type(result) == "table" and result.message or result), 2)
    end
end

use(fake)

local ok, result = generate({ prompt = "A lighthouse at dusk", seed = 7, width = 1000 })
succeeded(ok, result)
equal(#result.mediaIds, 1)
equal(result.seed, 7)
local provenance = result.provenance
equal(provenance.plugin, plugin)
equal(provenance.operation, "generate_image")
equal(provenance.params.prompt, "A lighthouse at dusk")
equal(provenance.tools.comfyui, "0.9.1")
equal(provenance.app.name, ms.run("app.info", {}).name)
equal(provenance.method.seed, 7)
equal(provenance.method.width, 992)
equal(provenance.promptId, "prompt-1")
equal(provenance.comfyui, fake)
equal(provenance.model.vae, "qwen_image_2.1_vae_bf16.safetensors")

local media = ms.project().media
local found
for _, item in ipairs(media) do
    if item.id == result.mediaIds[1] then
        found = item
    end
end
equal(found ~= nil, true)
equal(found.name, "A lighthouse at dusk")

local sent = prompts()[1].workflow
contains(sent["9"].inputs.filename_prefix, "mikserstudio/" .. ms.run("app.info", {}).name .. "/")
equal(sent["6"].inputs.seed, 7)
equal(sent["5"].inputs.width, 992)
for _, node in pairs(sent) do
    equal(node.class_type:sub(-3) ~= "Api", true)
end

-- A seed it picked itself is in the result and the provenance.
ok, result = generate({ prompt = "Another" })
succeeded(ok, result)
equal(math.type(result.seed), "integer")
equal(result.provenance.method.seed, result.seed)

setup({ missingModels = { "qwen_image_2.1_vae_bf16.safetensors" }, missingNodes = { "QwenImage21Cache" } })
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
contains(result.message, "ComfyUI lacks qwen_image_2.1_vae_bf16.safetensors, the node QwenImage21Cache.")
local check = ms.run(plugin .. ".check", {})
equal(check.available, false)
contains(check.reason, "ComfyUI lacks")

setup({ missingModels = ms.array({}), missingNodes = ms.array({}), refusePrompts = true })
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
equal(
    result.message,
    "ComfyUI did not take the job (Prompt outputs failed validation: Value not in list sampler_name: 'euler'). "
        .. "Check its models and nodes in ComfyUI and try again."
)

-- Canceling takes only the job's own prompt off ComfyUI's queue.
setup({ refusePrompts = false, holdPrompts = true })
local before = #prompts()
local held = start({ prompt = "Slow" })
local list = wait_for_prompts(before + 1)
ms.run("jobs.cancel", { jobId = held.id })
local deleted
for _ = 1, 500 do
    _, deleted = prompts()
    if #deleted > 0 then
        break
    end
end
equal(#deleted, 1)
equal(deleted[1], list[#list].id)
setup({ holdPrompts = false })

check = ms.run(plugin .. ".check", {})
equal(check.available, true)
equal(check.version, "0.9.1")

-- A new address applies to the jobs asked for after it.
use("http://127.0.0.1:9")
ok, result = generate({ prompt = "Hi" })
equal(ok, false)
contains(result.message, "ComfyUI is not reachable at http://127.0.0.1:9")
equal(result.message:find(".)", 1, true), nil)
check = ms.run(plugin .. ".check", {})
equal(check.available, false)
contains(check.reason, "ComfyUI is not reachable at http://127.0.0.1:9")
