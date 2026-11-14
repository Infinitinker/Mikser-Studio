-- ComfyUI's own text-to-image template for Qwen Image 2.1, without its prompt enhancer, in ComfyUI's API
-- format: the diffusion model through its cache, the Qwen 3 VL text encoder, an empty latent, KSampler
-- (euler, simple, cfg 1), the VAE, and SaveImage. It uses only ComfyUI's local nodes, never its API
-- nodes (whose names end in "Api").

local M = {}

M.DIFFUSION_MODEL = "qwen_image_2.1_int8_convrot.safetensors"
M.TEXT_ENCODER = "qwen3vl_8b_int8_convrot.safetensors"
M.VAE = "qwen_image_2.1_vae_bf16.safetensors"
-- The id of the SaveImage node, whose outputs /history lists.
M.SAVE_NODE = "9"

-- What the workflow needs from ComfyUI: {folder, file} for the models, and the nodes not in every
-- ComfyUI.
M.MODELS = {
    { folder = "diffusion_models", file = M.DIFFUSION_MODEL },
    { folder = "text_encoders", file = M.TEXT_ENCODER },
    { folder = "vae", file = M.VAE },
}
M.NODES = { "QwenImage21Cache", "TextEncodeQwenImage21" }

-- What ComfyUI is doing while it runs a node, by node id.
M.STAGES = {
    ["1"] = "Loading the image model",
    ["3"] = "Loading the text encoder",
    ["4"] = "Reading the prompt",
    ["7"] = "Loading the VAE",
    ["8"] = "Decoding the image",
    [M.SAVE_NODE] = "Saving the image",
}

-- A width or height as the model takes it: rounded down to a multiple of 16.
function M.model_size(pixels)
    return pixels // 16 * 16
end

local function node(class_type, inputs)
    return { class_type = class_type, inputs = inputs }
end

local function link(id, output)
    return { id, output }
end

-- The workflow for `request` ({prompt, negative_prompt, width, height, steps, seed}); SaveImage writes
-- into `prefix`, such as "mikserstudio/Mikser Studio/<job id>".
function M.image(request, prefix)
    return {
        ["1"] = node("UNETLoader", { unet_name = M.DIFFUSION_MODEL, weight_dtype = "default" }),
        ["2"] = node("QwenImage21Cache", { model = link("1", 0), device = "auto", dtype = "default" }),
        ["3"] = node("CLIPLoader", { clip_name = M.TEXT_ENCODER, type = "qwen_image" }),
        ["4"] = node("TextEncodeQwenImage21", {
            clip = link("3", 0),
            prompt = request.prompt,
            negative_prompt = request.negative_prompt,
            resolution = 1024,
        }),
        ["5"] = node("EmptyLatentImage", {
            width = M.model_size(request.width),
            height = M.model_size(request.height),
            batch_size = 1,
        }),
        ["6"] = node("KSampler", {
            model = link("2", 0),
            seed = request.seed,
            steps = request.steps,
            cfg = 1.0,
            sampler_name = "euler",
            scheduler = "simple",
            positive = link("4", 0),
            negative = link("4", 1),
            latent_image = link("5", 0),
            denoise = 1.0,
        }),
        ["7"] = node("VAELoader", { vae_name = M.VAE }),
        ["8"] = node("VAEDecode", { samples = link("6", 0), vae = link("7", 0) }),
        [M.SAVE_NODE] = node("SaveImage", { images = link("8", 0), filename_prefix = prefix }),
    }
end

return M
