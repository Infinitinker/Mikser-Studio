"""The comfyui backend against a fake ComfyUI: availability and what it lacks, the workflow it queues,
progress, the saved image and its provenance, ComfyUI's errors, and canceling a queued job. Never
talks to a real ComfyUI.
"""

import asyncio
import json
import socket
import uuid
from pathlib import Path
from typing import Any

import pytest
from aiohttp import web
from aiohttp.test_utils import TestServer
from pytest_aiohttp import AiohttpServer

from mikserstudio_ai.backend import Backend, BackendError, JobContext, Settings
from mikserstudio_ai.backends.comfyui import ComfyBackend, create
from mikserstudio_ai.backends.comfyui.client import ComfyClient
from mikserstudio_ai.backends.comfyui.text_to_image import DIFFUSION_MODEL, OPERATION, TEXT_ENCODER, VAE, workflow
from mikserstudio_ai.params import with_defaults

PNG = b"\x89PNG\r\n\x1a\nfake image"
MODELS = {"diffusion_models": [DIFFUSION_MODEL], "text_encoders": [TEXT_ENCODER], "vae": [VAE]}


class FakeComfy:
    """The parts of ComfyUI's API the backend uses. `outcome` says how a queued prompt ends:
    "success", "error" or "hold" (it stays queued until canceled)."""

    def __init__(self) -> None:
        self.models = {folder: list(names) for folder, names in MODELS.items()}
        self.nodes = {"QwenImage21Cache", "TextEncodeQwenImage21"}
        self.outcome = "success"
        self.prompts: list[dict[str, Any]] = []
        self.deleted: list[str] = []
        self.queued = asyncio.Event()
        self.sockets: dict[str, web.WebSocketResponse] = {}
        self.history: dict[str, Any] = {}

    def app(self) -> web.Application:
        app = web.Application()
        app.router.add_get("/system_stats", self.system_stats)
        app.router.add_get("/models/{folder}", self.list_models)
        app.router.add_get("/object_info/{node}", self.object_info)
        app.router.add_get("/ws", self.socket)
        app.router.add_post("/prompt", self.prompt)
        app.router.add_get("/history/{id}", self.get_history)
        app.router.add_get("/view", self.view)
        app.router.add_post("/queue", self.queue)
        return app

    async def system_stats(self, request: web.Request) -> web.Response:
        return web.json_response({"system": {"comfyui_version": "0.39.2"}})

    async def list_models(self, request: web.Request) -> web.Response:
        return web.json_response(self.models.get(request.match_info["folder"], []))

    async def object_info(self, request: web.Request) -> web.Response:
        node = request.match_info["node"]
        return web.json_response({node: {"input": {}}} if node in self.nodes else {})

    async def socket(self, request: web.Request) -> web.WebSocketResponse:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self.sockets[request.query["clientId"]] = ws
        await ws.send_json({"type": "status", "data": {"status": {"exec_info": {"queue_remaining": 0}}}})
        async for _ in ws:
            pass
        return ws

    async def prompt(self, request: web.Request) -> web.Response:
        body = await request.json()
        self.prompts.append(body)
        if body["prompt"]["4"]["inputs"]["prompt"] == "refuse":
            return web.json_response(
                {
                    "error": {
                        "type": "prompt_outputs_failed_validation",
                        "message": "Prompt outputs failed validation",
                    },
                    "node_errors": {"1": {"errors": [{"message": "Value not in list", "details": "unet_name"}]}},
                },
                status=400,
            )
        prompt_id = str(uuid.uuid4())
        self.queued.set()
        asyncio.get_running_loop().create_task(self.run(prompt_id, body))
        return web.json_response({"prompt_id": prompt_id, "number": 1, "node_errors": {}})

    async def run(self, prompt_id: str, body: dict[str, Any]) -> None:
        ws = self.sockets[body["client_id"]]
        if self.outcome == "hold":
            return
        await ws.send_json({"type": "execution_start", "data": {"prompt_id": prompt_id}})
        await ws.send_json({"type": "executing", "data": {"node": "1", "prompt_id": prompt_id}})
        for step in (1, 2):
            await ws.send_json({"type": "progress", "data": {"value": step, "max": 2, "prompt_id": prompt_id}})
        await ws.send_json({"type": "executing", "data": {"node": "8", "prompt_id": prompt_id}})
        if self.outcome == "error":
            data = {"prompt_id": prompt_id, "node_type": "KSampler", "exception_message": "Out of memory\n"}
            await ws.send_json({"type": "execution_error", "data": data})
            return
        prefix = body["prompt"]["9"]["inputs"]["filename_prefix"]
        folder, name = prefix.rsplit("/", 1)
        image = {"filename": f"{name}_00001_.png", "subfolder": folder, "type": "output"}
        self.history[prompt_id] = {"outputs": {"9": {"images": [image]}}, "status": {"completed": True}}
        await ws.send_json({"type": "executing", "data": {"node": None, "prompt_id": prompt_id}})

    async def get_history(self, request: web.Request) -> web.Response:
        prompt_id = request.match_info["id"]
        return web.json_response({prompt_id: self.history[prompt_id]} if prompt_id in self.history else {})

    async def view(self, request: web.Request) -> web.Response:
        assert request.query["type"] == "output"
        return web.Response(body=PNG, content_type="image/png")

    async def queue(self, request: web.Request) -> web.Response:
        self.deleted.extend((await request.json())["delete"])
        return web.json_response({})


class Recorder:
    def __init__(self) -> None:
        self.reports: list[tuple[float, str]] = []

    def __call__(self, fraction: float, message: str) -> None:
        self.reports.append((fraction, message))


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port: int = probe.getsockname()[1]
        return port


async def start(aiohttp_server: AiohttpServer, fake: FakeComfy) -> ComfyBackend:
    server: TestServer = await aiohttp_server(fake.app())
    return ComfyBackend(ComfyClient(str(server.make_url("")).rstrip("/")), output_folder="mikserstudio-test")


def job(tmp_path: Path, recorder: Recorder) -> JobContext:
    output = tmp_path / "job-1"
    output.mkdir()
    return JobContext("job-1", output, recorder)


def params(**given: Any) -> dict[str, Any]:
    return with_defaults({"prompt": "a red bicycle", **given}, OPERATION.params_schema)


async def test_a_comfyui_that_is_not_running_is_unavailable(tmp_path: Path) -> None:
    backend: Backend = create(Settings(cache_dir=tmp_path, comfyui_url=f"http://127.0.0.1:{free_port()}"))
    try:
        state = await backend.availability()
        assert not state.available
        assert "ComfyUI is not running" in state.reason
        with pytest.raises(BackendError, match="Start ComfyUI"):
            await backend.run("image.generate", params(), job(tmp_path, Recorder()))
    finally:
        await backend.close()


async def test_a_comfyui_without_the_models_or_nodes_says_what_it_lacks(
    aiohttp_server: AiohttpServer, tmp_path: Path
) -> None:
    fake = FakeComfy()
    fake.models["vae"] = []
    fake.nodes.discard("QwenImage21Cache")
    backend = await start(aiohttp_server, fake)
    state = await backend.availability()
    assert not state.available
    assert VAE in state.reason
    assert "the node QwenImage21Cache" in state.reason
    assert DIFFUSION_MODEL not in state.reason
    await backend.close()


async def test_a_complete_comfyui_is_available(aiohttp_server: AiohttpServer) -> None:
    backend = await start(aiohttp_server, FakeComfy())
    assert (await backend.availability()).available
    await backend.close()


async def test_an_image_is_made_saved_and_described(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeComfy()
    backend = await start(aiohttp_server, fake)
    recorder = Recorder()
    context = job(tmp_path, recorder)
    outcome = await backend.run("image.generate", params(seed=42, width=1000), context)
    await backend.close()

    queued = fake.prompts[0]["prompt"]
    assert queued["4"]["inputs"]["prompt"] == "a red bicycle"
    assert queued["5"]["inputs"] == {"width": 992, "height": 1024, "batch_size": 1}
    assert queued["6"]["inputs"]["seed"] == 42
    assert queued["9"]["inputs"]["filename_prefix"] == "mikserstudio-test/job-1"

    assert [output.kind for output in outcome.outputs] == ["image"]
    assert outcome.outputs[0].path == context.output_dir / "image-1.png"
    assert outcome.outputs[0].path.read_bytes() == PNG
    assert outcome.data == {"seed": 42}
    provenance = outcome.provenance
    assert provenance["tools"] == {"comfyui": "0.39.2"}
    assert provenance["model"]["name"] == DIFFUSION_MODEL
    assert provenance["method"]["seed"] == 42
    assert provenance["method"]["steps"] == 25

    messages = [message for _, message in recorder.reports]
    assert "Loading the image model" in messages
    assert "Step 2 of 2" in messages
    assert (0.95, "Decoding the image") in recorder.reports
    fractions = [fraction for fraction, _ in recorder.reports]
    assert fractions == sorted(fractions)
    assert recorder.reports[-1] == (1.0, "Done")


async def test_without_a_seed_each_image_gets_its_own_and_keeps_it(
    aiohttp_server: AiohttpServer, tmp_path: Path
) -> None:
    fake = FakeComfy()
    backend = await start(aiohttp_server, fake)
    outcome = await backend.run("image.generate", params(), job(tmp_path, Recorder()))
    await backend.close()
    assert outcome.data["seed"] == fake.prompts[0]["prompt"]["6"]["inputs"]["seed"]
    assert outcome.provenance["method"]["seed"] == outcome.data["seed"]


async def test_comfyui_refusing_the_workflow_says_why(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    backend = await start(aiohttp_server, FakeComfy())
    with pytest.raises(BackendError, match="Value not in list unet_name"):
        await backend.run("image.generate", params(prompt="refuse"), job(tmp_path, Recorder()))
    await backend.close()


async def test_an_error_in_comfyui_fails_the_job_with_its_message(
    aiohttp_server: AiohttpServer, tmp_path: Path
) -> None:
    fake = FakeComfy()
    fake.outcome = "error"
    backend = await start(aiohttp_server, fake)
    with pytest.raises(BackendError, match="Out of memory in KSampler"):
        await backend.run("image.generate", params(), job(tmp_path, Recorder()))
    await backend.close()


async def test_canceling_takes_only_this_job_off_comfyuis_queue(aiohttp_server: AiohttpServer, tmp_path: Path) -> None:
    fake = FakeComfy()
    fake.outcome = "hold"
    backend = await start(aiohttp_server, fake)
    running = asyncio.create_task(backend.run("image.generate", params(), job(tmp_path, Recorder())))
    await asyncio.wait_for(fake.queued.wait(), timeout=5)
    await asyncio.sleep(0.05)
    running.cancel()
    with pytest.raises(asyncio.CancelledError):
        await running
    await backend.close()
    assert len(fake.deleted) == 1


def test_the_workflow_is_the_qwen_image_template_without_its_prompt_enhancer() -> None:
    nodes = workflow(params(steps=8), 7, "mikserstudio/job-1")
    assert {node["class_type"] for node in nodes.values()} == {
        "UNETLoader",
        "QwenImage21Cache",
        "CLIPLoader",
        "TextEncodeQwenImage21",
        "EmptyLatentImage",
        "KSampler",
        "VAELoader",
        "VAEDecode",
        "SaveImage",
    }
    sampler = nodes["6"]["inputs"]
    assert (sampler["sampler_name"], sampler["scheduler"], sampler["cfg"], sampler["steps"]) == (
        "euler",
        "simple",
        1.0,
        8,
    )
    assert sampler["negative"] == ["4", 1]
    assert not any(node["class_type"].endswith("Api") for node in nodes.values())
    json.dumps(nodes)  # ComfyUI takes it as JSON.
