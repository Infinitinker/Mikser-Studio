"""Helpers shared by the tests."""

import asyncio
import os
import sys
from collections.abc import Callable
from pathlib import Path

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


def make_program(folder: Path, name: str) -> Path:
    """An empty executable file named like the program `name` on this system."""
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (f"{name}.exe" if sys.platform == "win32" else name)
    path.write_bytes(b"")
    path.chmod(0o755)
    return path


def same_file(found: str | None, expected: Path) -> bool:
    return found is not None and os.path.samefile(found, expected)
