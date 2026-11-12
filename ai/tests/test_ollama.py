"""The ollama backend against a fake Ollama: availability and reasons, model choice, saving the answer,
canceling the stream, and dropping reasoning before the answer.
"""

import asyncio
import json
import socket
from pathlib import Path
from typing import Any

import pytest
from aiohttp import web
from aiohttp.test_utils import TestServer
from pytest_aiohttp import AiohttpServer

from mikserstudio_ai.backend import Backend, BackendError, JobContext, Settings
from mikserstudio_ai.backends.ollama import create
from mikserstudio_ai.backends.ollama.generate import strip_reasoning

MODELS = [
    {"name": "tiny:latest", "digest": "sha256:aaa", "details": {"family": "llama", "parameter_size": "1B"}},
    {"name": "other:7b", "digest": "sha256:bbb", "details": {}},
]


class FakeOllama:
    """The parts of Ollama's API the backend uses. `hold` keeps /api/generate streaming until set."""

    def __init__(self, models: list[dict[str, Any]]) -> None:
        self.models = models
        self.requests: list[dict[str, Any]] = []
        self.hold: asyncio.Event | None = None
        self.disconnected = asyncio.Event()

    def app(self) -> web.Application:
        app = web.Application()
        app.router.add_get("/api/version", self.version)
        app.router.add_get("/api/tags", self.tags)
        app.router.add_post("/api/show", self.show)
        app.router.add_post("/api/generate", self.generate)
        return app

    async def version(self, request: web.Request) -> web.Response:
        return web.json_response({"version": "0.99.0"})

    async def tags(self, request: web.Request) -> web.Response:
        listed = [{key: value for key, value in model.items() if key != "capabilities"} for model in self.models]
        return web.json_response({"models": listed})

    async def show(self, request: web.Request) -> web.Response:
        name = (await request.json())["model"]
        model = next((model for model in self.models if model["name"] == name), None)
        if model is None:
            return web.json_response({"error": f"model '{name}' not found"}, status=404)
        details: dict[str, Any] = {"details": model["details"]}
        if "capabilities" in model:
            details["capabilities"] = model["capabilities"]
        return web.json_response(details)

    async def generate(self, request: web.Request) -> web.StreamResponse:
        body = await request.json()
        self.requests.append(body)
        if body["model"] not in {model["name"] for model in self.models}:
            return web.json_response({"error": f"model '{body['model']}' not found"}, status=404)
        response = web.StreamResponse(headers={"Content-Type": "application/x-ndjson"})
        await response.prepare(request)
        try:
            for word in ["Hello", " from", " ", body["model"]]:
                await response.write(json.dumps({"response": word, "done": False}).encode() + b"\n")
                if self.hold is not None:
                    await self.hold.wait()
            await response.write(json.dumps({"response": "", "done": True}).encode() + b"\n")
        except (ConnectionResetError, asyncio.CancelledError):
            self.disconnected.set()
            raise
        return response


class Recorder:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def __call__(self, fraction: float, message: str) -> None:
        self.messages.append(message)


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port: int = probe.getsockname()[1]
        return port


async def start(aiohttp_server: AiohttpServer, fake: FakeOllama, tmp_path: Path) -> Backend:
    server: TestServer = await aiohttp_server(fake.app())
    return create(Settings(cache_dir=tmp_path, ollama_url=str(server.make_url("")).rstrip("/")))


def job_context(tmp_path: Path, recorder: Recorder) -> JobContext:
    return JobContext("job-test", tmp_path, recorder)


async def test_not_running_ollama_is_unavailable_with_a_reason(tmp_path: Path) -> None:
    backend = create(Settings(cache_dir=tmp_path, ollama_url=f"http://127.0.0.1:{free_port()}"))
    try:
        state = await backend.availability()
        assert not state.available
        assert "Ollama is not running" in state.reason
        with pytest.raises(BackendError, match="Start Ollama"):
            await backend.run("text.generate", {"prompt": "hi"}, job_context(tmp_path, Recorder()))
    finally:
        await backend.close()


async def test_ollama_without_models_is_unavailable(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    backend = await start(aiohttp_server, FakeOllama([]), tmp_path)
    state = await backend.availability()
    assert not state.available
    assert "ollama pull" in state.reason
    await backend.close()


async def test_generate_uses_the_first_model_and_saves_the_answer(
    aiohttp_server: AiohttpServer, tmp_path: Path
) -> None:
    fake = FakeOllama(MODELS)
    backend = await start(aiohttp_server, fake, tmp_path)
    assert (await backend.availability()).available
    recorder = Recorder()
    outcome = await backend.run("text.generate", {"prompt": "Say hi"}, job_context(tmp_path, recorder))
    await backend.close()

    assert fake.requests == [{"model": "tiny:latest", "prompt": "Say hi", "stream": True}]
    assert outcome.data == {"text": "Hello from tiny:latest", "model": "tiny:latest"}
    [output] = outcome.outputs
    assert output.kind == "text"
    assert output.path.read_text(encoding="utf-8") == "Hello from tiny:latest\n"
    assert outcome.provenance == {
        "tools": {"ollama": "0.99.0"},
        "model": {"name": "tiny:latest", "digest": "sha256:aaa", "family": "llama", "parameterSize": "1B"},
    }
    assert recorder.messages[0] == "Loading tiny:latest"
    assert recorder.messages[-1].endswith("(5 tokens)")


async def test_generate_accepts_a_model_name_without_tag(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeOllama([{"name": "tiny:latest", "digest": "d", "details": {}}])
    backend = await start(aiohttp_server, fake, tmp_path)
    outcome = await backend.run("text.generate", {"prompt": "x", "model": "tiny"}, job_context(tmp_path, Recorder()))
    await backend.close()
    assert outcome.data["model"] == "tiny:latest"


async def test_unknown_model_lists_the_installed_ones(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    backend = await start(aiohttp_server, FakeOllama(MODELS), tmp_path)
    with pytest.raises(BackendError, match=r"not installed.*tiny:latest, other:7b.*ollama pull nope"):
        await backend.run("text.generate", {"prompt": "x", "model": "nope"}, job_context(tmp_path, Recorder()))
    await backend.close()


async def test_models_that_cannot_generate_text_are_skipped(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeOllama(
        [
            {"name": "vision:latest", "digest": "v", "details": {}, "capabilities": ["vision"]},
            {"name": "embed:latest", "digest": "e", "details": {}, "capabilities": ["embedding"]},
            {"name": "chat:latest", "digest": "c", "details": {}, "capabilities": ["completion", "tools"]},
        ]
    )
    backend = await start(aiohttp_server, fake, tmp_path)
    outcome = await backend.run("text.generate", {"prompt": "x"}, job_context(tmp_path, Recorder()))
    assert outcome.data["model"] == "chat:latest"

    with pytest.raises(BackendError, match=r'"vision:latest" cannot generate text \(it offers: vision\)'):
        await backend.run("text.generate", {"prompt": "x", "model": "vision"}, job_context(tmp_path, Recorder()))
    await backend.close()


async def test_no_model_that_can_generate_text_is_explained(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeOllama([{"name": "embed:latest", "digest": "e", "details": {}, "capabilities": ["embedding"]}])
    backend = await start(aiohttp_server, fake, tmp_path)
    with pytest.raises(BackendError, match="None of the installed Ollama models can generate text"):
        await backend.run("text.generate", {"prompt": "x"}, job_context(tmp_path, Recorder()))
    assert fake.requests == []
    await backend.close()


async def test_canceling_closes_the_stream(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeOllama(MODELS)
    fake.hold = asyncio.Event()
    backend = await start(aiohttp_server, fake, tmp_path)
    recorder = Recorder()
    task = asyncio.create_task(backend.run("text.generate", {"prompt": "x"}, job_context(tmp_path, recorder)))
    async with asyncio.timeout(10):
        while not any("tokens" in message for message in recorder.messages):
            await asyncio.sleep(0.01)
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    await asyncio.wait_for(fake.disconnected.wait(), 10)
    assert not (tmp_path / "response.txt").exists()
    await backend.close()


@pytest.mark.parametrize(
    ("raw", "answer"),
    [
        ("  hello from mikserstudio\n", "hello from mikserstudio"),
        ("Let me count the words.\n</think>\n\nhello from mikserstudio", "hello from mikserstudio"),
        ("<think>a</think>b</think> c", "c"),
    ],
)
def test_reasoning_before_think_end_is_dropped(raw: str, answer: str) -> None:
    assert strip_reasoning(raw) == answer
