"""The "ollama" backend: text generation with the models of a local Ollama server."""

from collections.abc import Mapping, Sequence
from typing import Any

from mikserstudio_ai.backend import Availability, Backend, BackendError, JobContext, Operation, Outcome, Settings
from mikserstudio_ai.backends.ollama.client import OllamaClient, OllamaError
from mikserstudio_ai.backends.ollama.generate import OPERATION, generate


class OllamaBackend:
    def __init__(self, client: OllamaClient) -> None:
        self._client = client

    @property
    def id(self) -> str:
        return "ollama"

    @property
    def title(self) -> str:
        return "Ollama"

    def operations(self) -> Sequence[Operation]:
        return [OPERATION]

    async def availability(self) -> Availability:
        try:
            await self._client.version()
            models = await self._client.models()
        except OllamaError:
            return Availability(
                False,
                f"Ollama is not running at {self._client.base_url}. Start Ollama (install it from "
                "https://ollama.com if needed) and try again.",
            )
        if not models:
            return Availability(False, 'Ollama has no models. Download one with "ollama pull <model>" and try again.')
        return Availability(True)

    async def run(self, op: str, params: Mapping[str, Any], context: JobContext) -> Outcome:
        if op != OPERATION.id:
            raise BackendError(f"The ollama backend does not offer “{op}”.")
        return await generate(self._client, params, context)

    async def close(self) -> None:
        await self._client.close()


def create(settings: Settings) -> Backend:
    return OllamaBackend(OllamaClient(settings.ollama_url))
