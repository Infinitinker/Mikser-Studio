"""Helpers shared by the tests."""

import asyncio
from collections.abc import Callable

from mikserstudio_ai.jobs import Job

TOKEN = "test-token"
AUTH = {"Authorization": f"Bearer {TOKEN}"}


async def wait_until(condition: Callable[[], bool], timeout: float = 10.0) -> None:
    """Polls condition until it is true; fails the test after timeout seconds."""
    async with asyncio.timeout(timeout):
        while not condition():
            await asyncio.sleep(0.01)


async def wait_finished(job: Job, timeout: float = 30.0) -> None:
    await wait_until(lambda: job.state.finished, timeout)
