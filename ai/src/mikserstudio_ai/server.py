"""The /v1 HTTP API: health, the backends with their operations, and the jobs."""

import asyncio
import json
from typing import Any

from aiohttp import web

from mikserstudio_ai import __version__
from mikserstudio_ai.auth import bearer_auth
from mikserstudio_ai.backend import Backend, Operation
from mikserstudio_ai.errors import ApiError, json_errors
from mikserstudio_ai.jobs import JobFinishedError, JobManager
from mikserstudio_ai.params import validate, with_defaults
from mikserstudio_ai.registry import BackendRegistry

REGISTRY = web.AppKey("registry", BackendRegistry)
JOBS = web.AppKey("jobs", JobManager)


def create_app(registry: BackendRegistry, jobs: JobManager, token: str) -> web.Application:
    app = web.Application(middlewares=[json_errors, bearer_auth(token)])
    app[REGISTRY] = registry
    app[JOBS] = jobs
    app.router.add_get("/v1/health", health)
    app.router.add_get("/v1/backends", backends)
    app.router.add_get("/v1/jobs", list_jobs)
    app.router.add_post("/v1/jobs", submit_job)
    app.router.add_get("/v1/jobs/{id}", get_job)
    app.router.add_delete("/v1/jobs/{id}", cancel_job)
    return app


async def health(request: web.Request) -> web.Response:
    return web.json_response({"status": "ok", "service": "mikserstudio-ai", "version": __version__})


async def backends(request: web.Request) -> web.Response:
    all_backends = request.app[REGISTRY].all()
    states = await asyncio.gather(*(backend.availability() for backend in all_backends))
    return web.json_response(
        [
            {
                "id": backend.id,
                "title": backend.title,
                "ops": [operation.to_json() for operation in backend.operations()],
                "available": state.available,
                "reason": state.reason,
            }
            for backend, state in zip(all_backends, states, strict=True)
        ]
    )


async def list_jobs(request: web.Request) -> web.Response:
    return web.json_response([job.to_json() for job in request.app[JOBS].all()])


async def get_job(request: web.Request) -> web.Response:
    job_id = request.match_info["id"]
    job = request.app[JOBS].get(job_id)
    if job is None:
        raise _unknown_job(job_id)
    return web.json_response(job.to_json())


async def cancel_job(request: web.Request) -> web.Response:
    job_id = request.match_info["id"]
    try:
        job = await request.app[JOBS].cancel(job_id)
    except JobFinishedError:
        raise ApiError(409, "already_finished", f"The job {job_id} has already finished.") from None
    if job is None:
        raise _unknown_job(job_id)
    return web.json_response(job.to_json())


async def submit_job(request: web.Request) -> web.Response:
    body = await _json_object(request)
    op = body.get("op")
    params = body.get("params", {})
    wanted_backend = body.get("backend")
    if not isinstance(op, str) or not op:
        raise ApiError(400, "invalid_params", "The body needs “op”, the id of an operation such as “video.upscale”.")
    if not isinstance(params, dict):
        raise ApiError(400, "invalid_params", "“params” must be a JSON object.")
    if wanted_backend is not None and not isinstance(wanted_backend, str):
        raise ApiError(400, "invalid_params", "“backend” must be the id of a backend.")

    backend, operation = await _choose_backend(request.app[REGISTRY], op, wanted_backend)
    effective = with_defaults(params, operation.params_schema)
    problems = validate(effective, operation.params_schema)
    if problems:
        raise ApiError(400, "invalid_params", f"Invalid parameters for “{op}”: " + "; ".join(problems))
    job = request.app[JOBS].submit(backend, op, effective)
    return web.json_response({"jobId": job.id}, status=202)


async def _choose_backend(registry: BackendRegistry, op: str, wanted: str | None) -> tuple[Backend, Operation]:
    """The wanted backend, or the first available one that offers op."""
    candidates = registry.offering(op)
    if wanted is not None:
        candidates = [(backend, operation) for backend, operation in candidates if backend.id == wanted]
    if not candidates:
        where = f"the backend “{wanted}”" if wanted else "any backend"
        raise ApiError(
            404, "unknown_op", f"The operation “{op}” is not offered by {where}. GET /v1/backends lists them."
        )
    reasons: list[str] = []
    for backend, operation in candidates:
        state = await backend.availability()
        if state.available:
            return backend, operation
        reasons.append(state.reason)
    raise ApiError(409, "unavailable", " ".join(reasons))


async def _json_object(request: web.Request) -> dict[str, Any]:
    try:
        body = await request.json()
    except json.JSONDecodeError as error:
        raise ApiError(400, "malformed_json", f"The body is not valid JSON: {error}.") from None
    if not isinstance(body, dict):
        raise ApiError(400, "invalid_params", "The body must be a JSON object.")
    return body


def _unknown_job(job_id: str) -> ApiError:
    return ApiError(404, "not_found", f"There is no job {job_id}. GET /v1/jobs lists the known jobs.")
