"""text.generate: one prompt, one answer, streamed from Ollama and saved as a text file."""

from collections.abc import Mapping
from typing import Any

from mikserstudio_ai.backend import BackendError, JobContext, Operation, Outcome, Output
from mikserstudio_ai.backends.ollama.client import Model, OllamaClient, OllamaError

THINK_END = "</think>"

OPERATION = Operation(
    id="text.generate",
    title="Generate Text",
    description="Answers a prompt with a local language model served by Ollama.",
    params_schema={
        "type": "object",
        "properties": {
            "prompt": {"type": "string", "minLength": 1, "description": "What to ask the model."},
            "model": {
                "type": "string",
                "minLength": 1,
                "description": "Ollama model name; the first installed model when omitted.",
            },
        },
        "required": ["prompt"],
        "additionalProperties": False,
    },
)


async def generate(client: OllamaClient, params: Mapping[str, Any], context: JobContext) -> Outcome:
    prompt = str(params["prompt"])
    try:
        version = await client.version()
        model = await _choose_model(client, await client.models(), params.get("model"))
    except OllamaError as error:
        raise BackendError(f"{error}. Start Ollama and try again.") from error

    context.report(0.0, f"Loading {model.name}")
    answer: list[str] = []
    chunks = 0
    try:
        async for chunk in client.generate(model.name, prompt):
            chunks += 1
            answer.append(chunk.response)
            what = "Thinking" if chunk.thinking and not chunk.response else "Writing"
            context.report(0.0, f"{what} with {model.name} ({chunks} tokens)")
    except OllamaError as error:
        raise BackendError(f"Ollama could not answer with {model.name}: {error}") from error

    text = strip_reasoning("".join(answer))
    output = context.output_dir / "response.txt"
    output.write_text(text + "\n", encoding="utf-8")
    return Outcome(
        outputs=[Output(output, "text")],
        provenance={
            "tools": {"ollama": version},
            "model": {"name": model.name, "digest": model.digest, **_model_details(model)},
        },
        data={"text": text, "model": model.name},
    )


async def _choose_model(client: OllamaClient, models: list[Model], wanted: object) -> Model:
    """The wanted model, or the first installed one that can generate text."""
    if not models:
        raise BackendError('Ollama has no models. Download one with "ollama pull <model>" and try again.')
    if wanted is None:
        for model in models:
            if await _can_generate(client, model) is None:
                return model
        raise BackendError(
            "None of the installed Ollama models can generate text. Download one with "
            '"ollama pull <model>" (for example a Llama, Qwen or Gemma model) and try again.'
        )
    for model in models:
        if model.name == wanted or model.name == f"{wanted}:latest":
            if (missing := await _can_generate(client, model)) is not None:
                raise BackendError(missing)
            return model
    installed = ", ".join(model.name for model in models)
    raise BackendError(
        f'The model "{wanted}" is not installed in Ollama (installed: {installed}). '
        f'Download it with "ollama pull {wanted}" or choose an installed one.'
    )


async def _can_generate(client: OllamaClient, model: Model) -> str | None:
    """None when the model can generate text (or Ollama does not say), else why not."""
    capabilities = await client.capabilities(model.name)
    if capabilities is None or "completion" in capabilities:
        return None
    offers = ", ".join(capabilities) or "nothing"
    return f'The Ollama model "{model.name}" cannot generate text (it offers: {offers}). Choose another model.'


def strip_reasoning(text: str) -> str:
    """The answer without the reasoning some thinking models write before "</think>" when Ollama
    does not separate it for them."""
    _, closed, answer = text.rpartition(THINK_END)
    return (answer if closed else text).strip()


def _model_details(model: Model) -> dict[str, str]:
    keys = {"family": "family", "parameter_size": "parameterSize", "quantization_level": "quantization"}
    return {name: str(model.details[key]) for key, name in keys.items() if model.details.get(key)}
