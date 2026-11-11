"""Finds the backends: each one announces itself through the "mikserstudio_ai.backends" entry point group."""

import logging
from collections.abc import Iterable, Sequence
from importlib.metadata import entry_points

from mikserstudio_ai.backend import Backend, Operation, Settings

logger = logging.getLogger(__name__)

GROUP = "mikserstudio_ai.backends"


class BackendRegistry:
    def __init__(self, backends: Iterable[Backend]) -> None:
        self._backends: dict[str, Backend] = {}
        for backend in backends:
            if backend.id in self._backends:
                logger.error("Two backends use the id %s; keeping the first one", backend.id)
                continue
            self._backends[backend.id] = backend

    @classmethod
    def from_entry_points(cls, settings: Settings) -> "BackendRegistry":
        """Every installed backend. One that cannot be loaded is logged and skipped, so a broken
        backend cannot stop the others or the service."""
        backends: list[Backend] = []
        for entry_point in entry_points(group=GROUP):
            try:
                backend = entry_point.load()(settings)
            except Exception:
                logger.exception("The backend %s cannot be loaded; skipping it", entry_point.value)
                continue
            if not isinstance(backend, Backend):
                logger.error("The backend %s (%s) is not a Backend; skipping it", entry_point.name, entry_point.value)
                continue
            backends.append(backend)
        return cls(backends)

    def all(self) -> Sequence[Backend]:
        return list(self._backends.values())

    def get(self, backend_id: str) -> Backend | None:
        return self._backends.get(backend_id)

    def offering(self, op: str) -> list[tuple[Backend, Operation]]:
        """The backends that offer op, in registration order, with their operation."""
        found: list[tuple[Backend, Operation]] = []
        for backend in self._backends.values():
            operation = next((item for item in backend.operations() if item.id == op), None)
            if operation is not None:
                found.append((backend, operation))
        return found

    async def close(self) -> None:
        for backend in self._backends.values():
            try:
                await backend.close()
            except Exception:
                logger.exception("The backend %s did not close cleanly", backend.id)
