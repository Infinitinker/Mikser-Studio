"""Finds ffmpeg/ffprobe, asks them about files and runs ffmpeg with progress reports."""

import asyncio
import json
import logging
from collections import deque
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from mikserstudio_ai.backend import BackendError
from mikserstudio_ai.processes import NO_WINDOW, find_program, kill, run_captured

logger = logging.getLogger(__name__)

STDERR_LINES_KEPT = 20
MISSING_FFPROBE = "ffprobe was not found next to ffmpeg or on PATH. Reinstall FFmpeg with ffprobe."


@dataclass(frozen=True, slots=True)
class VideoInfo:
    width: int
    height: int
    duration: float | None  # Seconds; None when the container does not say.


class Ffmpeg:
    def __init__(self, configured: str | None) -> None:
        self._configured = configured
        self._version: str | None = None
        self._encoders: str | None = None

    def executable(self) -> str | None:
        return find_program(self._configured or "ffmpeg")

    def ffprobe(self) -> str | None:
        """ffprobe next to ffmpeg, else the one on PATH."""
        ffmpeg = self.executable()
        if ffmpeg is not None:
            ffmpeg_path = Path(ffmpeg)
            sibling = find_program(ffmpeg_path.name.replace("ffmpeg", "ffprobe"), str(ffmpeg_path.parent))
            if sibling is not None:
                return sibling
        return find_program("ffprobe")

    async def version(self) -> str:
        if self._version is None:
            completed = await run_captured(self._require(), "-hide_banner", "-version")
            words = completed.stdout.split()
            self._version = words[2] if len(words) > 2 and words[0] == "ffmpeg" else "unknown"
        return self._version

    async def has_encoder(self, name: str) -> bool:
        if self._encoders is None:
            completed = await run_captured(self._require(), "-hide_banner", "-encoders")
            self._encoders = completed.stdout
        return any(line.split()[1:2] == [name] for line in self._encoders.splitlines())

    async def probe_video(self, path: Path) -> VideoInfo:
        ffprobe = self.ffprobe()
        if ffprobe is None:
            raise BackendError(MISSING_FFPROBE)
        completed = await run_captured(
            ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height:format=duration",
            "-of", "json", str(path),
        )  # fmt: skip
        if completed.returncode != 0:
            logger.error("ffprobe could not read %s: %s", path, completed.stderr.strip())
            raise BackendError(
                f"“{path.name}” cannot be read as a video. Check that it plays in a media player; Mikser Studio’s "
                "log has FFmpeg’s message."
            )
        data = json.loads(completed.stdout or "{}")
        streams = data.get("streams") or []
        if not streams:
            raise BackendError(f"“{path.name}” has no video in it. Choose a clip with a picture.")
        duration = data.get("format", {}).get("duration")
        return VideoInfo(
            width=int(streams[0]["width"]),
            height=int(streams[0]["height"]),
            duration=float(duration) if duration not in (None, "N/A") else None,
        )

    async def run(self, arguments: Sequence[str], duration: float | None, report: Callable[[float], None]) -> None:
        """Runs ffmpeg with arguments, calling report(fraction) as it goes.

        Raises BackendError with ffmpeg's last error line when it fails. Canceling kills ffmpeg."""
        command = [self._require(), "-hide_banner", "-nostdin", "-nostats", "-progress", "pipe:1", *arguments]
        logger.debug("Running %s", command)
        process = await asyncio.create_subprocess_exec(
            *command,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            creationflags=NO_WINDOW,
        )
        assert process.stdout is not None and process.stderr is not None
        stderr_tail: deque[str] = deque(maxlen=STDERR_LINES_KEPT)
        try:
            await asyncio.gather(
                _read_progress(process.stdout, duration, report),
                _collect_lines(process.stderr, stderr_tail),
            )
            returncode = await process.wait()
        except asyncio.CancelledError:
            await kill(process)
            raise
        if returncode != 0:
            # The detail is for the log, which the app keeps; the message says what to do.
            logger.error("ffmpeg exited with code %d. Its last output:\n%s", returncode, "\n".join(stderr_tail))
            raise BackendError(
                "FFmpeg could not convert the video. Check that the "
                "video plays in a media player and that the drive has free space, then try again; Mikser Studio’s "
                "log has FFmpeg’s full output."
            )

    def _require(self) -> str:
        executable = self.executable()
        if executable is None:
            raise BackendError(missing_ffmpeg_reason(self._configured))
        return executable


def missing_ffmpeg_reason(configured: str | None) -> str:
    if configured:
        return f"ffmpeg was not found at “{configured}”. Check the --ffmpeg path of the AI service."
    return "ffmpeg was not found. Install FFmpeg (https://ffmpeg.org) and add it to PATH, then restart Mikser Studio."


async def _read_progress(stream: asyncio.StreamReader, duration: float | None, report: Callable[[float], None]) -> None:
    """Parses ffmpeg's -progress output: key=value lines, each block ending with progress=..."""
    position = 0.0
    async for raw in stream:
        key, _, value = raw.decode(errors="replace").strip().partition("=")
        if key == "out_time_us" and value.lstrip("-").isdigit():
            position = max(0.0, int(value) / 1_000_000)
        elif key == "progress":
            if value == "end":
                report(1.0)
            elif duration:
                report(min(position / duration, 0.99))


async def _collect_lines(stream: asyncio.StreamReader, tail: deque[str]) -> None:
    async for raw in stream:
        line = raw.decode(errors="replace").rstrip()
        if line:
            tail.append(line)
