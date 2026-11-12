"""The job manager: one job per backend at a time, backends side by side, cancellation, readable failures,
and how many finished jobs it keeps.
"""

import asyncio
from pathlib import Path

import pytest

from mikserstudio_ai.jobs import MAX_FINISHED_JOBS, JobFinishedError, JobManager, JobState
from tests.fakes import FakeBackend
from tests.support import wait_finished, wait_until


async def test_jobs_of_one_backend_run_one_at_a_time(cache_dir: Path) -> None:
    backend = FakeBackend()
    backend.gate = asyncio.Event()
    jobs = JobManager(cache_dir)
    first = jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    second = jobs.submit(backend, "test.echo", {"text": "b", "times": 1, "behavior": "succeed"})
    await wait_until(lambda: first.state == JobState.RUNNING)
    await asyncio.sleep(0.05)
    assert second.state == JobState.QUEUED
    assert second.progress == 0.0

    backend.gate.set()
    await wait_finished(second)
    assert (first.state, second.state) == (JobState.SUCCEEDED, JobState.SUCCEEDED)
    assert first.started_at is not None and first.finished_at is not None
    assert (cache_dir / first.id / "echo.txt").read_text(encoding="utf-8") == "a"


async def test_jobs_of_different_backends_run_side_by_side(cache_dir: Path) -> None:
    one, two = FakeBackend("one"), FakeBackend("two")
    one.gate, two.gate = asyncio.Event(), asyncio.Event()
    jobs = JobManager(cache_dir)
    first = jobs.submit(one, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    second = jobs.submit(two, "test.echo", {"text": "b", "times": 1, "behavior": "succeed"})
    await wait_until(lambda: first.state == JobState.RUNNING and second.state == JobState.RUNNING)
    await jobs.close()


async def test_canceling_a_queued_job_never_runs_it(cache_dir: Path) -> None:
    backend = FakeBackend()
    backend.gate = asyncio.Event()
    jobs = JobManager(cache_dir)
    first = jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    second = jobs.submit(backend, "test.echo", {"text": "b", "times": 1, "behavior": "succeed"})
    await wait_until(lambda: first.state == JobState.RUNNING)

    canceled = await jobs.cancel(second.id)
    assert canceled is second
    assert second.state == JobState.CANCELED
    assert second.started_at is None
    assert first.state == JobState.RUNNING

    backend.gate.set()
    await wait_finished(first)
    assert backend.canceled == 0


async def test_cancel_refuses_finished_and_ignores_unknown_jobs(cache_dir: Path) -> None:
    jobs = JobManager(cache_dir)
    job = jobs.submit(FakeBackend(), "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    await wait_finished(job)
    with pytest.raises(JobFinishedError):
        await jobs.cancel(job.id)
    assert await jobs.cancel("job-unknown") is None


async def test_unexpected_errors_fail_the_job_with_a_readable_message(cache_dir: Path) -> None:
    jobs = JobManager(cache_dir)
    job = jobs.submit(FakeBackend(), "test.echo", {"text": "a", "times": 1, "behavior": "crash"})
    await wait_finished(job)
    assert job.state == JobState.FAILED
    assert job.error is not None and "boom" in job.error and "log" in job.error


async def test_close_cancels_everything(cache_dir: Path) -> None:
    backend = FakeBackend()
    backend.gate = asyncio.Event()
    jobs = JobManager(cache_dir)
    running = jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    queued = jobs.submit(backend, "test.echo", {"text": "b", "times": 1, "behavior": "succeed"})
    await wait_until(lambda: running.state == JobState.RUNNING)
    await jobs.close()
    assert running.state == JobState.CANCELED
    assert queued.state == JobState.CANCELED


async def test_old_finished_jobs_are_forgotten(cache_dir: Path) -> None:
    jobs = JobManager(cache_dir)
    backend = FakeBackend()
    first = jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    await wait_finished(first)
    for _ in range(MAX_FINISHED_JOBS):
        await wait_finished(jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"}))
    jobs.submit(backend, "test.echo", {"text": "a", "times": 1, "behavior": "succeed"})
    assert jobs.get(first.id) is None
    assert len(jobs.all()) == MAX_FINISHED_JOBS + 1
    await jobs.close()
