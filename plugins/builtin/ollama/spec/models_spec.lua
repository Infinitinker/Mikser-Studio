-- The models command lists the installed models with their family, size and quantization, says so
-- clearly when Ollama isn't there, and the settings take only http addresses.

local plugin = "mikserstudio.ollama"
local fake = ms.settings().fakes.ollama

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

ms.run("plugins.set_settings", { id = plugin, settings = { url = fake } })
local models = ms.run(plugin .. ".models", {}).models
equal(#models, 2)
equal(models[1].name, "vision-only:latest")
equal(models[2].name, "llama3:latest")
equal(models[2].digest, "sha256:llama3:latest")
equal(models[2].family, "llama")
equal(models[2].parameterSize, "8.0B")
equal(models[2].quantization, "Q4_K_M")

ms.run("plugins.set_settings", { id = plugin, settings = { url = "http://127.0.0.1:9" } })
local ok, problem = pcall(ms.run, plugin .. ".models", {})
equal(ok, false)
equal(problem.code, "failed")
contains(problem.message, "Ollama is not reachable at http://127.0.0.1:9")

ok, problem = pcall(ms.run, "plugins.set_settings", { id = plugin, settings = { url = "ftp://example.com" } })
equal(ok, false)
equal(problem.code, "invalid_params")
