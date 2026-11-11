"""A small client for the parts of Ollama's HTTP API the backend uses (/api/version, /api/tags,
/api/generate), see https://github.com/ollama/ollama/blob/main/docs/api.md."""

import json
from collections.abc import AsyncIterator
from dataclasses import dataclass
from typing import Any

import aiohttp

# Short, so a missing Ollama is noticed quickly when the backends are listed.
QUICK = aiohttp.ClientTimeout(total=3)
# /api/show reads the model file's metadata, which can take a moment on a cold disk.
SHOW = aiohttp.ClientTimeout(total=15)
# Loading a large model before the first token can take minutes; there is no total limit.
GENERATE = aiohttp.ClientTimeout(total=None, sock_connect=5, sock_read=600)


class OllamaError(Exception):
    """Ollama answered with an error, or could not be reached."""


@dataclass(frozen=True, slots=True)
class Model:
    name: str
    digest: str
    details: dict[str, Any]


@dataclass(frozen=True, slots=True)
class Chunk:
    response: str  # Answer text in this chunk.
    thinking: str  # Reasoning text of thinking models, not part of the answer.
    done: bool


class OllamaClient:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url
        self._session: aiohttp.ClientSession | None = None

    async def version(self) -> str:
        data = await self._get_json("/api/version")
        return str(data.get("version", "unknown"))

    async def models(self) -> list[Model]:
        data = await self._get_json("/api/tags")
        return [
            Model(name=str(item.get("name", "")), digest=str(item.get("digest", "")), details=item.get("details") or {})
            for item in data.get("models") or []
            if item.get("name")
        ]

    async def capabilities(self, model: str) -> list[str] | None:
        """What the model can do ("completion", "vision", "embedding", ...); None when this Ollama
        does not say."""
        try:
            async with self._client().post(
                f"{self.base_url}/api/show", json={"model": model}, timeout=SHOW
            ) as response:
                if response.status != 200:
                    raise OllamaError(_error_text(await response.text(), response.status))
                data = await response.json(content_type=None)
        except (aiohttp.ClientError, TimeoutError) as error:
            raise OllamaError(
                f"Ollama did not describe the model {model} ({str(error) or type(error).__name__})"
            ) from error
        capabilities = data.get("capabilities") if isinstance(data, dict) else None
        return [str(item) for item in capabilities] if isinstance(capabilities, list) else None

    async def generate(self, model: str, prompt: str) -> AsyncIterator[Chunk]:
        """Streams the answer. Breaking out of the loop closes the connection, which stops Ollama."""
        try:
            async with self._client().post(
                f"{self.base_url}/api/generate",
                json={"model": model, "prompt": prompt, "stream": True},
                timeout=GENERATE,
            ) as response:
                if response.status != 200:
                    raise OllamaError(_error_text(await response.text(), response.status))
                async for raw in response.content:
                    line = raw.strip()
                    if not line:
                        continue
                    data = json.loads(line)
                    if "error" in data:
                        raise OllamaError(str(data["error"]))
                    yield Chunk(str(data.get("response", "")), str(data.get("thinking", "")), bool(data.get("done")))
        except aiohttp.ClientError as error:
            raise OllamaError(f"the connection to Ollama failed ({error})") from error

    async def close(self) -> None:
        if self._session is not None:
            await self._session.close()
            self._session = None

    async def _get_json(self, path: str) -> dict[str, Any]:
        try:
            async with self._client().get(f"{self.base_url}{path}", timeout=QUICK) as response:
                if response.status != 200:
                    raise OllamaError(_error_text(await response.text(), response.status))
                data = await response.json(content_type=None)
        except (aiohttp.ClientError, TimeoutError) as error:
            reason = str(error) or type(error).__name__
            raise OllamaError(f"Ollama is not reachable at {self.base_url} ({reason})") from error
        if not isinstance(data, dict):
            raise OllamaError(f"Ollama answered {path} with something that is not a JSON object")
        return data

    def _client(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession()
        return self._session


def _error_text(body: str, status: int) -> str:
    try:
        return str(json.loads(body)["error"])
    except (ValueError, KeyError, TypeError):
        return f"HTTP {status}: {body.strip()[:200]}"
