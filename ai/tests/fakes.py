"""A scriptable backend for the API and job tests."""

import asyncio
from collections.abc import Mapping, Sequence
from typing import Any

from mikserstudio_ai.backend import Availability, BackendError, JobContext, Operation, Outcome, Output

ECHO = Operation(
    id="test.echo",
    title="Echo",
    description="Writes the text to a file.",
    params_schema={
        "type": "object",
        "properties": {
            "text": {"type": "string", "minLength": 1},
            "times": {"type": "integer", "minimum": 1, "default": 1},
            "behavior": {"type": "string", "enum": ["succeed", "fail", "crash"], "default": "succeed"},
        },
        "required": ["text"],
        "additionalProperties": False,
    },
)


class FakeBackend:
    """Runs test.echo. While `gate` is set to an unset Event, jobs wait for it after reporting 0.5."""

    def __init__(self, backend_id: str = "fake", available: bool = True, reason: str = "") -> None:
        self._id = backend_id
        self.state = Availability(available, reason)
        self.gate: asyncio.Event | None = None
        self.started = asyncio.Event()
        self.canceled = 0
        self.closed = False

    @property
    def id(self) -> str:
        return self._id

    @property
    def title(self) -> str:
        return f"Fake {self._id}"

    def operations(self) -> Sequence[Operation]:
        return [ECHO]

    async def availability(self) -> Availability:
        return self.state

    async def run(self, op: str, params: Mapping[str, Any], context: JobContext) -> Outcome:
        self.started.set()
        context.report(0.5, "Halfway")
        try:
            if self.gate is not None:
                await self.gate.wait()
        except asyncio.CancelledError:
            self.canceled += 1
            raise
        if params["behavior"] == "fail":
            raise BackendError("The fake backend was told to fail.")
        if params["behavior"] == "crash":
            raise RuntimeError("boom")
        output = context.output_dir / "echo.txt"
        output.write_text(params["text"] * params["times"], encoding="utf-8")
        return Outcome([Output(output, "text")], {"tools": {"fake": "1.0"}}, {"text": params["text"]})

    async def close(self) -> None:
        self.closed = True
