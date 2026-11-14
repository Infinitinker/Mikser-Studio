-- A thinking model's reasoning is dropped from its answer, and an answer that stops inside <think> is
-- told apart (nil), since the model ran out of room before it answered.

local ollama = require("ollama")

local function equal(actual, expected)
    if actual ~= expected then
        error(("expected %s, got %s"):format(tostring(expected), tostring(actual)), 2)
    end
end

equal(ollama.strip_reasoning("<think>hmm</think>\n Answer "), "Answer")
equal(ollama.strip_reasoning("Just an answer"), "Just an answer")
equal(ollama.strip_reasoning("<think>a</think><think>b</think> c"), "c")
equal(ollama.strip_reasoning("<think>still thinking"), nil)
equal(ollama.strip_reasoning("<think>a</think> b <think>c"), nil)
equal(ollama.strip_reasoning(""), "")
