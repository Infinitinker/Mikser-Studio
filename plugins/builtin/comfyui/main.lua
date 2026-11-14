-- The ComfyUI plugin: "generate_image" makes a still image from a prompt with Qwen Image 2.1 in
-- ComfyUI, as a job that brings it into the project with its provenance, and "check" says whether
-- ComfyUI has what that needs. The address of ComfyUI is the "url" setting; a job uses the one set
-- when it was asked for.

local comfyui = require("comfyui")

local function size(description)
    return { type = "integer", minimum = 256, maximum = 2048, default = 1024, description = description }
end

ms.command({
    id = "mikserstudio.comfyui.generate_image",
    title = "Generate image",
    description = "Makes a still image from a prompt with Qwen Image 2.1 in ComfyUI and adds it to the media bin. "
        .. "The work goes on in a job, whose result is {mediaIds, seed, outputs, provenance}.",
    category = "AI",
    permission = "network",
    open_world = true,
    starts_job = true,
    params = {
        type = "object",
        properties = {
            prompt = { type = "string", minLength = 1, description = "What the image shows." },
            negative_prompt = { type = "string", default = "", description = "What the image should not show." },
            width = size("In pixels, rounded down to a multiple of 16."),
            height = size("In pixels, rounded down to a multiple of 16."),
            steps = { type = "integer", minimum = 1, maximum = 100, default = 25, description = "Sampling steps." },
            seed = {
                type = "integer",
                minimum = 0,
                maximum = comfyui.MAX_SEED,
                description = "The noise seed; a new one each time when omitted. The provenance keeps it.",
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
        local session = ms.run("project.session", {}).session
        local app = ms.run("app.info", {})
        local started = ms.jobs.run({
            kind = "generate_image",
            title = "Generate image: " .. comfyui.shortened(params.prompt, 60),
            lane = "generate",
        }, function(job)
            return comfyui.generate(base, params, job, session, app)
        end)
        return { jobId = started.id }
    end,
})

ms.command({
    id = "mikserstudio.comfyui.check",
    title = "Check ComfyUI",
    description = "Says whether ComfyUI runs and has the models and nodes that generate_image needs, and why not.",
    category = "AI",
    permission = "network",
    read_only = true,
    open_world = true,
    result = {
        type = "object",
        properties = {
            available = { type = "boolean" },
            version = { type = "string" },
            reason = { type = "string" },
        },
        required = { "available" },
    },
    handler = function()
        local ok, info = pcall(comfyui.inspect, ms.settings().url)
        if not ok then
            return { available = false, reason = type(info) == "table" and info.message or tostring(info) }
        end
        if #info.lacking > 0 then
            return { available = false, version = info.version, reason = comfyui.lacks(info.lacking) }
        end
        return { available = true, version = info.version }
    end,
})
