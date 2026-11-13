"""The running Mikser Studio, as its local API shows it: its commands, running them and its events."""

import json
import logging
from collections.abc import AsyncIterator, Awaitable, Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import aiohttp

from mikserstudio_ai.mcp.discovery import Discovery, read_discovery

log = logging.getLogger("mikserstudio.mcp")

JsonObject = dict[str, Any]

# Commands that start AI jobs answer at once; importing media probes the file first.
RUN_TIMEOUT = aiohttp.ClientTimeout(total=120)
LIST_TIMEOUT = aiohttp.ClientTimeout(total=10)

NOT_RUNNING = "Mikser Studio is not running, or its local API is off. Start Mikser Studio, then try again."


class AppUnavailable(Exception):
    """The app could not be reached; the message says what to do."""


@dataclass(frozen=True)
class CommandError:
    """A command the app refused or that failed: the API's error code and message."""

    code: str
    message: str


class App:
    """Talks to the Mikser Studio whose discovery file is at `discovery_path`, reading it anew each
    time, since the app writes a new port and token at each start."""

    def __init__(self, session: aiohttp.ClientSession, discovery_path: Path) -> None:
        self._session = session
        self._discovery_path = discovery_path

    def _discovery(self) -> Discovery:
        discovery = read_discovery(self._discovery_path)
        if discovery is None:
            raise AppUnavailable(NOT_RUNNING)
        return discovery

    async def commands(self) -> list[JsonObject]:
        """Every command the app offers now, as `GET /v1/commands` lists them."""
        discovery = self._discovery()
        try:
            async with self._session.get(
                f"{discovery.base_url}/v1/commands", headers=_auth(discovery), timeout=LIST_TIMEOUT
            ) as response:
                if response.status != 200:
                    raise AppUnavailable(f"Mikser Studio did not list its commands (HTTP {response.status}).")
                listed = await response.json()
        except (aiohttp.ClientError, TimeoutError, ValueError) as error:
            raise AppUnavailable(NOT_RUNNING) from error
        if not isinstance(listed, list):
            raise AppUnavailable("Mikser Studio sent a command list this bridge cannot read.")
        return [command for command in listed if isinstance(command, dict)]

    async def run(self, command_id: str, params: JsonObject) -> JsonObject | CommandError:
        """Runs a command: its result, or the error the app answered with."""
        discovery = self._discovery()
        try:
            async with self._session.post(
                f"{discovery.base_url}/v1/commands/{command_id}",
                headers=_auth(discovery),
                json=params,
                timeout=RUN_TIMEOUT,
            ) as response:
                body = await response.json(content_type=None)
        except TimeoutError as error:
            raise AppUnavailable(f"Mikser Studio did not answer “{command_id}” within two minutes.") from error
        except (aiohttp.ClientError, ValueError) as error:
            raise AppUnavailable(NOT_RUNNING) from error
        if not isinstance(body, dict):
            raise AppUnavailable("Mikser Studio sent an answer this bridge cannot read.")
        if response.status == 200:
            result = body.get("result")
            return result if isinstance(result, dict) else {"result": result}
        refused = body.get("error")
        if isinstance(refused, dict):
            return CommandError(code=str(refused.get("code", "failed")), message=str(refused.get("message", "")))
        return CommandError(code="failed", message=f"Mikser Studio answered HTTP {response.status}.")

    async def events(self, on_connected: Callable[[], Awaitable[None]]) -> AsyncIterator[JsonObject]:
        """The app's events until its socket closes; `on_connected` runs once the socket is open.
        Raises AppUnavailable when the app cannot be reached."""
        discovery = self._discovery()
        try:
            async with self._session.ws_connect(discovery.events_url, headers=_auth(discovery)) as socket:
                await on_connected()
                async for message in socket:
                    if message.type != aiohttp.WSMsgType.TEXT:
                        continue
                    try:
                        event = json.loads(message.data)
                    except ValueError:
                        log.warning("Ignored an event that is not JSON.")
                        continue
                    if isinstance(event, dict):
                        yield event
        except (aiohttp.ClientError, TimeoutError) as error:
            raise AppUnavailable(NOT_RUNNING) from error


def _auth(discovery: Discovery) -> dict[str, str]:
    return {"Authorization": f"Bearer {discovery.token}"}
