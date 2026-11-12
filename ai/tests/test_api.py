import asyncio
from pathlib import Path
from typing import Any

import pytest
from aiohttp import web
from aiohttp.test_utils import TestClient
from pytest_aiohttp import AiohttpClient

from mikserstudio_ai.jobs import JobManager
from mikserstudio_ai.registry import BackendRegistry
from mikserstudio_ai.server import create_app
from tests.fakes import FakeBackend
from tests.support import AUTH, TOKEN

type Client = TestClient[web.Request, web.Application]


@pytest.fixture
def backend() -> FakeBackend:
    return FakeBackend()


@pytest.fixture
def jobs(cache_dir: Path) -> JobManager:
    return JobManager(cache_dir)


@pytest.fixture
async def client(aiohttp_client: AiohttpClient, backend: FakeBackend, jobs: JobManager) -> Client:
    app = create_app(BackendRegistry([backend]), jobs, TOKEN)
    return await aiohttp_client(app)


async def job_json(client: Client, job_id: str) -> dict[str, Any]:
    response = await client.get(f"/v1/jobs/{job_id}", headers=AUTH)
    assert response.status == 200
    data: dict[str, Any] = await response.json()
    return data


async def finished_job(client: Client, job_id: str) -> dict[str, Any]:
    """Polls the job like the Mikser Studio app does until it has finished."""
    async with asyncio.timeout(10):
        while (job := await job_json(client, job_id))["state"] in ("queued", "running"):
            await asyncio.sleep(0.01)
    return job


async def submit(client: Client, body: dict[str, Any]) -> str:
    response = await client.post("/v1/jobs", json=body, headers=AUTH)
    assert response.status == 202, await response.text()
    job_id: str = (await response.json())["jobId"]
    return job_id


async def test_health_needs_no_token(client: Client) -> None:
    response = await client.get("/v1/health")
    assert response.status == 200
    assert (await response.json())["status"] == "ok"


@pytest.mark.parametrize("header", [None, "Bearer wrong", f"Basic {TOKEN}", TOKEN])
async def test_other_routes_need_the_token(client: Client, header: str | None) -> None:
    headers = {"Authorization": header} if header else {}
    response = await client.get("/v1/backends", headers=headers)
    assert response.status == 401
    assert response.headers["WWW-Authenticate"] == "Bearer"
    assert (await response.json())["error"]["code"] == "unauthorized"


async def test_backends_lists_operations_and_availability(client: Client, backend: FakeBackend) -> None:
    backend.state = type(backend.state)(False, "Start the fake service.")
    response = await client.get("/v1/backends", headers=AUTH)
    assert response.status == 200
    [listed] = await response.json()
    assert listed["id"] == "fake"
    assert listed["available"] is False
    assert listed["reason"] == "Start the fake service."
    assert [op["id"] for op in listed["ops"]] == ["test.echo"]
    assert listed["ops"][0]["paramsSchema"]["required"] == ["text"]


async def test_job_runs_to_success_with_outputs_and_provenance(client: Client) -> None:
    job_id = await submit(client, {"op": "test.echo", "params": {"text": "hi", "times": 2}})
    job = await finished_job(client, job_id)
    assert job["state"] == "succeeded"
    assert job["progress"] == 1.0
    assert job["op"] == "test.echo"
    assert job["backend"] == "fake"
    assert job["error"] is None
    assert job["params"] == {"text": "hi", "times": 2, "behavior": "succeed"}
    result = job["result"]
    assert result["text"] == "hi"
    [output] = result["outputs"]
    assert output["kind"] == "text"
    assert Path(output["path"]).read_text(encoding="utf-8") == "hihi"
    provenance = result["provenance"]
    assert provenance["backend"] == "fake"
    assert provenance["op"] == "test.echo"
    assert provenance["params"] == job["params"]
    assert provenance["tools"] == {"fake": "1.0"}
    assert provenance["jobId"] == job_id
    assert provenance["createdAt"].endswith("Z")


async def test_jobs_lists_every_job(client: Client) -> None:
    first = await submit(client, {"op": "test.echo", "params": {"text": "a"}})
    second = await submit(client, {"op": "test.echo", "params": {"text": "b"}})
    response = await client.get("/v1/jobs", headers=AUTH)
    assert [job["id"] for job in await response.json()] == [first, second]


async def test_running_job_reports_progress_and_can_be_canceled(client: Client, backend: FakeBackend) -> None:
    backend.gate = asyncio.Event()
    job_id = await submit(client, {"op": "test.echo", "params": {"text": "slow"}})
    await asyncio.wait_for(backend.started.wait(), 5)
    running = await job_json(client, job_id)
    assert running["state"] == "running"
    assert running["progress"] == 0.5
    assert running["message"] == "Halfway"

    response = await client.delete(f"/v1/jobs/{job_id}", headers=AUTH)
    assert response.status == 200
    assert (await response.json())["state"] == "canceled"
    assert backend.canceled == 1

    again = await client.delete(f"/v1/jobs/{job_id}", headers=AUTH)
    assert again.status == 409
    assert (await again.json())["error"]["code"] == "already_finished"


async def test_failed_job_carries_the_backend_message(client: Client) -> None:
    job_id = await submit(client, {"op": "test.echo", "params": {"text": "x", "behavior": "fail"}})
    job = await finished_job(client, job_id)
    assert job["state"] == "failed"
    assert job["error"] == "The fake backend was told to fail."
    assert job["result"] is None


@pytest.mark.parametrize(
    ("body", "status", "code"),
    [
        ({"params": {}}, 400, "invalid_params"),
        ({"op": "test.echo", "params": []}, 400, "invalid_params"),
        ({"op": "no.such_op"}, 404, "unknown_op"),
        ({"op": "test.echo", "backend": "other", "params": {"text": "x"}}, 404, "unknown_op"),
        ({"op": "test.echo", "params": {"text": ""}}, 400, "invalid_params"),
        ({"op": "test.echo", "params": {"text": "x", "color": "red"}}, 400, "invalid_params"),
    ],
)
async def test_bad_job_requests_are_refused(client: Client, body: dict[str, Any], status: int, code: str) -> None:
    response = await client.post("/v1/jobs", json=body, headers=AUTH)
    assert response.status == status
    error = (await response.json())["error"]
    assert error["code"] == code
    assert error["message"]


async def test_invalid_params_name_every_problem(client: Client) -> None:
    response = await client.post("/v1/jobs", json={"op": "test.echo", "params": {"times": 0, "extra": 1}}, headers=AUTH)
    message = (await response.json())["error"]["message"]
    assert "text: is required" in message
    assert "times: must be >= 1" in message
    assert "extra: is not a known parameter" in message


async def test_malformed_json_is_refused(client: Client) -> None:
    response = await client.post("/v1/jobs", data=b"{nope", headers={**AUTH, "Content-Type": "application/json"})
    assert response.status == 400
    assert (await response.json())["error"]["code"] == "malformed_json"


async def test_unavailable_backend_refuses_jobs_with_its_reason(client: Client, backend: FakeBackend) -> None:
    backend.state = type(backend.state)(False, "Start the fake service.")
    response = await client.post("/v1/jobs", json={"op": "test.echo", "params": {"text": "x"}}, headers=AUTH)
    assert response.status == 409
    error = (await response.json())["error"]
    assert error == {"code": "unavailable", "message": "Start the fake service."}


async def test_unknown_job_and_route_answer_404(client: Client) -> None:
    for method, path in [("GET", "/v1/jobs/job-x"), ("DELETE", "/v1/jobs/job-x"), ("GET", "/v1/nothing")]:
        response = await client.request(method, path, headers=AUTH)
        assert response.status == 404
        assert (await response.json())["error"]["code"] == "not_found"


async def test_first_available_backend_offering_the_op_runs_it(aiohttp_client: AiohttpClient, cache_dir: Path) -> None:
    offline = FakeBackend("offline", available=False, reason="offline")
    online = FakeBackend("online")
    client = await aiohttp_client(create_app(BackendRegistry([offline, online]), JobManager(cache_dir), TOKEN))
    job_id = await submit(client, {"op": "test.echo", "params": {"text": "x"}})
    assert (await job_json(client, job_id))["backend"] == "online"

    response = await client.post(
        "/v1/jobs", json={"op": "test.echo", "backend": "offline", "params": {"text": "x"}}, headers=AUTH
    )
    assert response.status == 409
