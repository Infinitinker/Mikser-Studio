"""The "builtin" backend: operations that need only ffmpeg, no AI model."""

from collections.abc import Mapping, Sequence
from typing import Any

from mikserstudio_ai.backend import Availability, Backend, BackendError, JobContext, Operation, Outcome, Settings
from mikserstudio_ai.backends.builtin.ffmpeg import MISSING_FFPROBE, Ffmpeg, missing_ffmpeg_reason
from mikserstudio_ai.backends.builtin.upscale import ENCODER, OPERATION, upscale


class BuiltinBackend:
    def __init__(self, ffmpeg: Ffmpeg, configured_ffmpeg: str | None) -> None:
        self._ffmpeg = ffmpeg
        self._configured_ffmpeg = configured_ffmpeg

    @property
    def id(self) -> str:
        return "builtin"

    @property
    def title(self) -> str:
        return "Built-in (FFmpeg)"

    def operations(self) -> Sequence[Operation]:
        return [OPERATION]

    async def availability(self) -> Availability:
        if self._ffmpeg.executable() is None:
            return Availability(False, missing_ffmpeg_reason(self._configured_ffmpeg))
        if self._ffmpeg.ffprobe() is None:
            return Availability(False, MISSING_FFPROBE)
        if not await self._ffmpeg.has_encoder(ENCODER):
            return Availability(False, f"This ffmpeg has no {ENCODER} encoder. Install an FFmpeg build with {ENCODER}.")
        return Availability(True)

    async def run(self, op: str, params: Mapping[str, Any], context: JobContext) -> Outcome:
        if op != OPERATION.id:
            raise BackendError(f'The builtin backend does not offer "{op}".')
        return await upscale(self._ffmpeg, params, context)

    async def close(self) -> None:
        return None


def create(settings: Settings) -> Backend:
    return BuiltinBackend(Ffmpeg(settings.ffmpeg), settings.ffmpeg)
