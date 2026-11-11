"""Finds and runs helper programs (ffmpeg, ffprobe) without console windows and collects their output."""

import asyncio
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass

# The sidecar runs without a console when Mikser Studio starts it; without this flag every child process
# would open its own console window on Windows.
NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def find_program(name: str, search_path: str | None = None) -> str | None:
    """The full path of a program given by name ("ffmpeg") or by absolute path, else None.

    A name is looked up only in the absolute folders of search_path (default: PATH). Unlike
    shutil.which on Windows, it never looks in the working directory, so a program planted in the
    folder Mikser Studio was started from cannot stand in for ffmpeg. A relative path is refused."""
    if os.path.dirname(name):
        return shutil.which(name) if os.path.isabs(name) else None
    folders = os.environ.get("PATH", "") if search_path is None else search_path
    for folder in folders.split(os.pathsep):
        if folder and os.path.isabs(folder):
            # With a folder in the name, shutil.which checks only that folder.
            found = shutil.which(os.path.join(folder, name))
            if found is not None:
                return found
    return None


@dataclass(frozen=True, slots=True)
class Completed:
    returncode: int
    stdout: str
    stderr: str


async def run_captured(*command: str) -> Completed:
    """Runs command to the end. Cancelling the caller kills the process."""
    process = await asyncio.create_subprocess_exec(
        *command,
        stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        creationflags=NO_WINDOW,
    )
    try:
        stdout, stderr = await process.communicate()
    except asyncio.CancelledError:
        await kill(process)
        raise
    return Completed(process.returncode or 0, stdout.decode(errors="replace"), stderr.decode(errors="replace"))


async def kill(process: asyncio.subprocess.Process) -> None:
    if process.returncode is None:
        try:
            process.kill()
        except ProcessLookupError:
            pass
    await process.wait()
