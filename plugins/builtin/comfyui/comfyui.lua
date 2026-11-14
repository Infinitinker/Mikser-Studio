-- Talking to ComfyUI: whether it has what the workflow needs, running the workflow in a
-- comfyui.run_prompt job, and bringing its images into the project with their provenance.

local workflow = require("workflow")

local M = {}

local QUICK = 5 -- seconds: a missing ComfyUI is noticed quickly
-- The seed's range, as ComfyUI's KSampler takes it.
M.MAX_SEED = (1 << 48) - 1

local REPLACED = "The project was replaced before the image was done, so it was not added. Its file is in "
    .. "Mikser Studio’s cache; open the original project and generate it again."

local function endpoint(base, path)
    return (base:gsub("/+$", "")) .. path
end

-- A job's error as a clause for another sentence: its words without the closing period.
local function message_of(problem)
    local message = type(problem) == "table" and problem.message or tostring(problem)
    return (message:gsub("%.$", ""))
end

-- Raises `problem` again when it is a cancel, so a canceled job ends as canceled.
local function rethrow_cancel(problem)
    if type(problem) == "table" and problem.code == "canceled" then
        error(problem, 0)
    end
end

-- Starts a GET of ComfyUI's API; its :await() gives the answer.
local function get(base, path)
    return ms.jobs.start("http.request", { url = endpoint(base, path), timeout = QUICK, title = "ComfyUI: " .. path })
end

-- The JSON of an answer, or an error that names what failed.
local function json_of(answer, path)
    if answer.status ~= 200 or answer.json == nil then
        error(("%s answered HTTP %d"):format(path, answer.status), 0)
    end
    return answer.json
end

-- What ComfyUI is and lacks: {version, lacking = {names}}, asking for its version, the models and
-- the nodes all at once. Raises an error that says ComfyUI isn't reachable.
function M.inspect(base)
    local ok, info = pcall(function()
        local stats = get(base, "/system_stats")
        local models, nodes = {}, {}
        for i, model in ipairs(workflow.MODELS) do
            models[i] = get(base, "/models/" .. model.folder)
        end
        for i, name in ipairs(workflow.NODES) do
            nodes[i] = get(base, "/object_info/" .. name)
        end
        local system = json_of(stats:await(), "/system_stats").system or {}
        local lacking = {}
        for i, model in ipairs(workflow.MODELS) do
            local files = json_of(models[i]:await(), "/models/" .. model.folder)
            local found = false
            for _, file in ipairs(files) do
                found = found or file == model.file
            end
            if not found then
                table.insert(lacking, model.file)
            end
        end
        for i, name in ipairs(workflow.NODES) do
            if json_of(nodes[i]:await(), "/object_info/" .. name)[name] == nil then
                table.insert(lacking, "the node " .. name)
            end
        end
        return { version = system.comfyui_version or "unknown", lacking = lacking }
    end)
    if not ok then
        rethrow_cancel(info)
        error(("ComfyUI is not reachable at %s (%s). Start ComfyUI and try again."):format(base, message_of(info)), 0)
    end
    return info
end

local LACKS = "ComfyUI lacks %s. Add them in ComfyUI, for example from its Qwen Image 2.1 template, and try again."

function M.lacks(lacking)
    return LACKS:format(table.concat(lacking, ", "))
end

-- What ComfyUI said when it refused the workflow: its message and the first node error.
function M.refusal(refused)
    local answer = refused.json or {}
    local problem = answer.error
    local message = type(problem) == "table" and problem.message or problem
    for _, node in pairs(answer.node_errors or {}) do
        local first = (node.errors or {})[1]
        if first then
            message = ("%s: %s %s"):format(message or "", first.message or "", first.details or "")
            break
        end
    end
    if message == nil or message == "" then
        message = ("HTTP %d: %s"):format(refused.status, (refused.body or ""):sub(1, 200))
    end
    return (message:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- At most `limit` characters of `text`, on one line.
function M.shortened(text, limit)
    local line = text:gsub("%s+", " ")
    if utf8.len(line) and utf8.len(line) > limit then
        return line:sub(1, utf8.offset(line, limit + 1) - 1) .. "…"
    end
    return line
end

-- Makes the image of `params` with the ComfyUI at `base`, in the job `job` that the command asked for
-- in the project session `session` of the app `app` (app.info's {name, version}), and brings it into
-- the project as one undo step. Returns {mediaIds, seed, outputs, provenance}.
function M.generate(base, params, job, session, app)
    local request = {
        prompt = params.prompt,
        negative_prompt = params.negative_prompt or "",
        width = params.width or 1024,
        height = params.height or 1024,
        steps = params.steps or 25,
        seed = params.seed or math.random(0, M.MAX_SEED),
    }
    job:progress(0, "Checking ComfyUI")
    local info = M.inspect(base)
    if #info.lacking > 0 then
        error(M.lacks(info.lacking), 0)
    end

    -- Each instance of Mikser Studio has its own folder in ComfyUI's output, with one per job.
    local prefix = ("mikserstudio/%s/%s"):format(app.name, job.id)
    local ok, ran = pcall(function()
        return ms.jobs
            .start("comfyui.run_prompt", {
                url = base,
                workflow = workflow.image(request, prefix),
                outputNode = workflow.SAVE_NODE,
                stages = workflow.STAGES,
                title = "ComfyUI: Qwen Image 2.1",
            })
            :await()
    end)
    if not ok then
        rethrow_cancel(ran)
        error(type(ran) == "table" and ran.message or tostring(ran), 0)
    end
    if ran.refused then
        error(
            ("ComfyUI did not take the job (%s). Check its models and nodes in ComfyUI and try again."):format(
                M.refusal(ran.refused)
            ),
            0
        )
    end

    local provenance = {
        plugin = ms.plugin_id,
        operation = "generate_image",
        params = request,
        app = { name = app.name, version = app.version },
        tools = { comfyui = info.version },
        model = { name = workflow.DIFFUSION_MODEL, textEncoder = workflow.TEXT_ENCODER, vae = workflow.VAE },
        method = {
            workflow = "Qwen Image 2.1 text to image",
            sampler = "euler",
            scheduler = "simple",
            cfg = 1.0,
            steps = request.steps,
            seed = request.seed,
            width = workflow.model_size(request.width),
            height = workflow.model_size(request.height),
        },
        createdAt = os.date("!%Y-%m-%dT%H:%M:%SZ"),
    }
    -- What the job saw itself, which the plugin can't change.
    for key, value in pairs(ran.provenance) do
        provenance[key] = value
    end

    local name = M.shortened(params.prompt, 60)
    local media = ms.array({})
    ms.transaction("Add AI result", function()
        if ms.run("project.session", {}).session ~= session then
            error(REPLACED, 0)
        end
        for i, output in ipairs(ran.outputs) do
            local named = #ran.outputs == 1 and name or ("%s (%d)"):format(name, i)
            local imported =
                ms.run("project.import_media", { path = output.path, name = named, provenance = provenance })
            table.insert(media, imported.mediaId)
        end
    end)
    return { mediaIds = media, seed = request.seed, outputs = ran.outputs, provenance = provenance }
end

return M
