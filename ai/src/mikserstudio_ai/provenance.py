"""Where a generated file came from, stored with every job result and later on the media item."""

from collections.abc import Mapping
from datetime import UTC, datetime
from typing import Any, TypedDict

from mikserstudio_ai import __version__
from mikserstudio_ai.backend import BackendProvenance, JsonObject


class ServiceInfo(TypedDict):
    name: str
    version: str


class Provenance(BackendProvenance):
    """A job's provenance as clients read it: these fields, plus what the backend reported."""

    jobId: str  # The JSON field names are camelCase.
    backend: str
    op: str
    params: JsonObject
    createdAt: str
    service: ServiceInfo


def utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")


def make_provenance(
    job_id: str, backend: str, op: str, params: Mapping[str, Any], extra: BackendProvenance
) -> Provenance:
    """The common fields plus what the backend reports (tool versions, model)."""
    return Provenance(
        jobId=job_id,
        backend=backend,
        op=op,
        params=dict(params),
        createdAt=utc_now(),
        service=ServiceInfo(name="mikserstudio-ai", version=__version__),
        **extra,
    )
