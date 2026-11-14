-- The Qwen Image 2.1 workflow: its sizes, the request's values in their nodes, ComfyUI's local nodes
-- only, and the words for a refusal.

local workflow = require("workflow")
local comfyui = require("comfyui")

local function equal(actual, expected)
    if actual ~= expected then
        error(("expected %s, got %s"):format(tostring(expected), tostring(actual)), 2)
    end
end

equal(workflow.model_size(1000), 992)
equal(workflow.model_size(1024), 1024)

local graph = workflow.image(
    { prompt = "A fox", negative_prompt = "blur", width = 512, height = 777, steps = 9, seed = 42 },
    "mikserstudio/Test/job-1"
)
equal(graph["4"].inputs.prompt, "A fox")
equal(graph["4"].inputs.negative_prompt, "blur")
equal(graph["5"].inputs.height, 768)
equal(graph["6"].inputs.steps, 9)
equal(graph["6"].inputs.seed, 42)
equal(graph["6"].inputs.positive[1], "4")
equal(graph[workflow.SAVE_NODE].inputs.filename_prefix, "mikserstudio/Test/job-1")
for _, node in pairs(graph) do
    equal(node.class_type:sub(-3) ~= "Api", true)
end

equal(comfyui.refusal({ status = 500, body = "Internal error" }), "HTTP 500: Internal error")
equal(comfyui.refusal({ status = 400, json = { error = "Bad prompt" } }), "Bad prompt")
equal(comfyui.shortened("one  two\nthree", 60), "one two three")
equal(comfyui.shortened(("é"):rep(70), 60), ("é"):rep(60) .. "…")
