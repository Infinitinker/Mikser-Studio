"""The contract every AI backend implements, and the data it exchanges with the job runner.

A backend offers operations ("video.upscale", "text.generate"), says whether it can run right now,
and runs one operation at a time for a job. It never touches HTTP or the job list: the server and
the JobManager do that, so adding a backend is one module plus one entry point.
"""

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol, TypedDict, runtime_checkable

type JsonObject = dict[str, Any]


class BackendError(Exception):
    """A job failed for a reason the user can act on; str(error) is shown to them as is."""


@dataclass(frozen=True, slots=True)
class Operation:
    id: str  # Dotted, shared by backends that offer the same thing: "video.upscale".
    title: str  # Short English UI label.
    description: str  # One sentence for tooltips and docs.
    params_schema: JsonObject  # JSON Schema of the params object (the subset in params.py).

    def to_json(self) -> JsonObject:
        return {
            "id": self.id,
            "title": self.title,
            "description": self.description,
            "paramsSchema": self.params_schema,
        }


@dataclass(frozen=True, slots=True)
class Availability:
    available: bool
    reason: str = ""  # Why it is not available and what to do about it; empty when available.


@dataclass(frozen=True, slots=True)
class Output:
    path: Path
    kind: str  # "video", "image", "audio" or "text".

    def to_json(self) -> JsonObject:
        return {"path": self.path.as_posix(), "kind": self.kind}


class BackendProvenance(TypedDict, total=False):
    """What a backend adds to a job's provenance (provenance.Provenance); each key is optional."""

    tools: dict[str, str]  # The programs used and their versions: {"ffmpeg": "7.1.1"}.
    model: JsonObject  # The model used: "name", "digest" and what else the backend knows of it.
    method: JsonObject  # How the result was made when it says more than the model: filter, encoder.


@dataclass(frozen=True, slots=True)
class Outcome:
    """What a finished operation produced."""

    outputs: list[Output]
    # Merged into the job's provenance.
    provenance: BackendProvenance = field(default_factory=lambda: BackendProvenance())
    # The operation's own result fields for the client, such as the generated "text". Their keys depend
    # on the operation, so each backend types them for its operations.
    data: Mapping[str, object] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class JobContext:
    job_id: str
    output_dir: Path  # Empty folder in the cache, owned by this job; created before run().
    report: Callable[[float, str], None]  # Progress 0..1 and a short English status message.


@dataclass(frozen=True, slots=True)
class Settings:
    """What the sidecar knows from its command line; each backend takes the parts it needs."""

    cache_dir: Path
    ffmpeg: str | None = None  # Path of ffmpeg; None means "find it on PATH".
    ollama_url: str = "http://127.0.0.1:11434"


@runtime_checkable
class Backend(Protocol):
    @property
    def id(self) -> str: ...

    @property
    def title(self) -> str: ...

    def operations(self) -> Sequence[Operation]: ...

    async def availability(self) -> Availability: ...

    async def run(self, op: str, params: Mapping[str, Any], context: JobContext) -> Outcome:
        """Runs op with params that already passed its schema (defaults applied).

        Raises BackendError for failures the user can fix. Cancellation arrives as
        asyncio.CancelledError: stop child processes, remove partial outputs and re-raise."""
        ...

    async def close(self) -> None:
        """Releases connections and other resources when the sidecar stops."""
        ...


type BackendFactory = Callable[[Settings], Backend]
