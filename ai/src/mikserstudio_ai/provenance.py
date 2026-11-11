"""Where a generated file came from, stored with every job result and later on the media item."""

from collections.abc import Mapping
from datetime import UTC, datetime
from typing import Any

from mikserstudio_ai import __version__
from mikserstudio_ai.backend import JsonObject


def utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")


def make_provenance(job_id: str, backend: str, op: str, params: Mapping[str, Any], extra: JsonObject) -> JsonObject:
    """The common fields plus what the backend reports (tool versions, model)."""
    provenance: JsonObject = {
        "jobId": job_id,
        "backend": backend,
        "op": op,
        "params": dict(params),
        "createdAt": utc_now(),
        "service": {"name": "mikserstudio-ai", "version": __version__},
    }
    provenance.update(extra)
    return provenance
