"""Runs jobs in the background: one at a time per backend, the rest wait in the queue."""

import asyncio
import logging
import uuid
from collections.abc import Mapping
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path
from typing import Any

from mikserstudio_ai.backend import Backend, BackendError, JobContext, JsonObject, Output
from mikserstudio_ai.provenance import Provenance, make_provenance, utc_now

logger = logging.getLogger(__name__)

# Finished jobs kept for clients to read; older ones are forgotten (their files stay in the cache).
MAX_FINISHED_JOBS = 200


class JobState(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    SUCCEEDED = "succeeded"
    FAILED = "failed"
    CANCELED = "canceled"

    @property
    def finished(self) -> bool:
        return self in (JobState.SUCCEEDED, JobState.FAILED, JobState.CANCELED)


class JobFinishedError(Exception):
    """The job has already finished, so it cannot be canceled."""


@dataclass(frozen=True, slots=True)
class JobResult:
    """What a job that succeeded produced. In JSON, the operation's own fields sit next to "outputs" and
    "provenance"."""

    outputs: list[Output]
    provenance: Provenance
    data: Mapping[str, object] = field(default_factory=dict)

    def to_json(self) -> JsonObject:
        return {
            **self.data,
            "outputs": [output.to_json() for output in self.outputs],
            "provenance": self.provenance,
        }


@dataclass(slots=True)
class Job:
    id: str
    op: str
    backend: str
    params: JsonObject
    state: JobState = JobState.QUEUED
    progress: float = 0.0
    message: str = "Waiting for other jobs to finish"
    created_at: str = field(default_factory=utc_now)
    started_at: str | None = None
    finished_at: str | None = None
    result: JobResult | None = None
    error: str | None = None

    def to_json(self) -> JsonObject:
        return {
            "id": self.id,
            "op": self.op,
            "backend": self.backend,
            "params": self.params,
            "state": str(self.state),
            "progress": self.progress,
            "message": self.message,
            "createdAt": self.created_at,
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "result": self.result.to_json() if self.result else None,
            "error": self.error,
        }


class JobManager:
    def __init__(self, cache_dir: Path) -> None:
        self._cache_dir = cache_dir
        self._jobs: dict[str, Job] = {}
        self._tasks: dict[str, asyncio.Task[None]] = {}
        self._locks: dict[str, asyncio.Lock] = {}

    def submit(self, backend: Backend, op: str, params: Mapping[str, Any]) -> Job:
        job = Job(id=f"job-{uuid.uuid4().hex}", op=op, backend=backend.id, params=dict(params))
        self._jobs[job.id] = job
        self._tasks[job.id] = asyncio.create_task(self._run(job, backend), name=job.id)
        self._forget_old_jobs()
        logger.info("Queued %s (%s on %s)", job.id, op, backend.id)
        return job

    def get(self, job_id: str) -> Job | None:
        return self._jobs.get(job_id)

    def all(self) -> list[Job]:
        return list(self._jobs.values())

    async def cancel(self, job_id: str) -> Job | None:
        """Cancels a queued or running job and waits until it has stopped; None if unknown."""
        job = self._jobs.get(job_id)
        if job is None:
            return None
        if job.state.finished:
            raise JobFinishedError(job_id)
        task = self._tasks.get(job_id)
        if task is not None:
            task.cancel()
            await asyncio.wait([task])
        return job

    async def close(self) -> None:
        tasks = list(self._tasks.values())
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.wait(tasks)

    async def _run(self, job: Job, backend: Backend) -> None:
        try:
            async with self._locks.setdefault(backend.id, asyncio.Lock()):
                await self._execute(job, backend)
        except asyncio.CancelledError:
            self._finish(job, JobState.CANCELED, "Canceled")
            logger.info("Canceled %s", job.id)
        except BackendError as error:
            self._finish(job, JobState.FAILED, "Failed", error=str(error))
            logger.warning("%s failed: %s", job.id, error)
        except Exception as error:
            logger.exception("%s stopped with an unexpected error", job.id)
            self._finish(
                job,
                JobState.FAILED,
                "Failed",
                error=f"The AI service hit an unexpected error ({error}). See the AI service log for details.",
            )
        finally:
            self._tasks.pop(job.id, None)

    async def _execute(self, job: Job, backend: Backend) -> None:
        job.state = JobState.RUNNING
        job.started_at = utc_now()
        job.message = "Starting"
        output_dir = self._cache_dir / job.id
        output_dir.mkdir(parents=True, exist_ok=True)

        def report(fraction: float, message: str) -> None:
            job.progress = min(1.0, max(0.0, fraction))
            if message:
                job.message = message

        outcome = await backend.run(job.op, job.params, JobContext(job.id, output_dir, report))
        job.result = JobResult(
            outputs=outcome.outputs,
            provenance=make_provenance(job.id, backend.id, job.op, job.params, outcome.provenance),
            data=outcome.data,
        )
        job.progress = 1.0
        self._finish(job, JobState.SUCCEEDED, "Done")
        logger.info("%s succeeded", job.id)

    @staticmethod
    def _finish(job: Job, state: JobState, message: str, error: str | None = None) -> None:
        job.state = state
        job.message = message
        job.error = error
        job.finished_at = utc_now()

    def _forget_old_jobs(self) -> None:
        finished = [job for job in self._jobs.values() if job.state.finished]
        for job in finished[: max(0, len(finished) - MAX_FINISHED_JOBS)]:
            del self._jobs[job.id]
