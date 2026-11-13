"""The MCP side of the bridge: answers a client's JSON-RPC messages with the app's commands as tools."""

import asyncio
import contextlib
import json
import logging
from collections.abc import Awaitable, Callable
from itertools import count
from typing import Any

from mikserstudio_ai import __version__
from mikserstudio_ai.mcp.app import App, AppUnavailable, CommandError
from mikserstudio_ai.mcp.tools import tools_for

log = logging.getLogger("mikserstudio.mcp")

JsonObject = dict[str, Any]
Send = Callable[[JsonObject], Awaitable[None]]

# The protocol versions this bridge speaks, newest first. Structured results came with 2025-06-18.
PROTOCOL_VERSIONS = ("2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05")
STRUCTURED_SINCE = "2025-06-18"

# How long to wait before looking for the app again once it is gone.
RETRY_SECONDS = 2.0
# How long requests still running may take to answer once the client has closed its input.
CLOSE_SECONDS = 10.0

INSTRUCTIONS = (
    "These tools run Mikser Studio's commands in the project the person has open, the same commands its "
    "menus run. Read the project with project_list before changing it. Every change shows in the app, and "
    "undoable ones can be taken back with edit_undo. AI commands start a job and return its id: follow it "
    "with ai_jobs. When no tools are listed, Mikser Studio is not running; the list fills in once it starts."
)

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602


class Bridge:
    """One client's session. `receive` takes each message from the client; `send` writes one to it."""

    def __init__(self, app: App, send: Send) -> None:
        self._app = app
        self._send = send
        self._protocol = PROTOCOL_VERSIONS[0]
        self._can_ask = False  # Whether the client can ask the person (elicitation).
        self._ready = False  # Notifications go out only after the client's `initialized`.
        self._connected = False
        self._ids = count(1)
        self._asked: dict[str, asyncio.Future[JsonObject]] = {}
        self._running: dict[Any, asyncio.Task[None]] = {}

    async def receive(self, line: bytes) -> None:
        try:
            message = json.loads(line)
        except ValueError:
            await self._error(None, PARSE_ERROR, "The message is not JSON.")
            return
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
            await self._error(None, INVALID_REQUEST, "The message is not a JSON-RPC 2.0 object.")
            return
        if "method" not in message:
            self._answered(message)
        elif "id" in message:
            request_id = message["id"]
            task = asyncio.create_task(self._request(request_id, message["method"], message.get("params") or {}))
            self._running[request_id] = task
            task.add_done_callback(lambda _: self._running.pop(request_id, None))
        else:
            self._notified(message["method"], message.get("params") or {})

    async def watch_app(self) -> None:
        """Follows the app's events, and tells the client when its tools change: when the app starts or
        stops, and when plugins add or remove commands."""
        while True:
            try:
                async for event in self._app.events(on_connected=self._app_started):
                    if event.get("type") in ("command.registered", "command.unregistered"):
                        await self._tools_changed()
            except AppUnavailable:
                pass
            if self._connected:
                self._connected = False
                log.info("Mikser Studio stopped.")
                await self._tools_changed()
            await asyncio.sleep(RETRY_SECONDS)

    async def close(self) -> None:
        """Ends the session once the client has gone: questions to the person count as unanswered,
        and requests still running get a little time to send their answers."""
        for asked in self._asked.values():
            if not asked.done():
                asked.set_result({"action": "cancel"})
        running = list(self._running.values())
        if running:
            _, late = await asyncio.wait(running, timeout=CLOSE_SECONDS)
            for task in late:
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task

    async def _app_started(self) -> None:
        self._connected = True
        log.info("Connected to Mikser Studio.")
        await self._tools_changed()

    async def _tools_changed(self) -> None:
        if self._ready:
            await self._send({"jsonrpc": "2.0", "method": "notifications/tools/list_changed"})

    def _notified(self, method: str, params: JsonObject) -> None:
        if method == "notifications/initialized":
            self._ready = True
        elif method == "notifications/cancelled":
            task = self._running.get(params.get("requestId"))
            if task:
                task.cancel()

    def _answered(self, message: JsonObject) -> None:
        asked = self._asked.pop(str(message.get("id")), None)
        if asked is None or asked.done():
            return
        if isinstance(message.get("result"), dict):
            asked.set_result(message["result"])
        else:
            asked.set_result({"action": "cancel"})

    async def _request(self, request_id: Any, method: str, params: JsonObject) -> None:
        try:
            if method == "initialize":
                result = self._initialize(params)
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = await self._list_tools()
            elif method == "tools/call":
                result = await self._call_tool(params)
            else:
                await self._error(request_id, METHOD_NOT_FOUND, f"This bridge has no method “{method}”.")
                return
        except _InvalidParams as error:
            await self._error(request_id, INVALID_PARAMS, str(error))
            return
        except asyncio.CancelledError:
            return  # The client canceled the request: it expects no answer.
        await self._send({"jsonrpc": "2.0", "id": request_id, "result": result})

    def _initialize(self, params: JsonObject) -> JsonObject:
        asked_for = params.get("protocolVersion")
        self._protocol = asked_for if asked_for in PROTOCOL_VERSIONS else PROTOCOL_VERSIONS[0]
        capabilities = params.get("capabilities")
        self._can_ask = isinstance(capabilities, dict) and isinstance(capabilities.get("elicitation"), dict)
        return {
            "protocolVersion": self._protocol,
            "capabilities": {"tools": {"listChanged": True}},
            "serverInfo": {"name": "mikser-studio", "title": "Mikser Studio", "version": __version__},
            "instructions": INSTRUCTIONS,
        }

    async def _list_tools(self) -> JsonObject:
        try:
            tools, _ = tools_for(await self._app.commands())
        except AppUnavailable:
            tools = []
        return {"tools": tools}

    async def _call_tool(self, params: JsonObject) -> JsonObject:
        name = params.get("name")
        arguments = params.get("arguments") or {}
        if not isinstance(name, str) or not isinstance(arguments, dict):
            raise _InvalidParams("tools/call needs a tool name and an object of arguments.")
        try:
            _, by_name = tools_for(await self._app.commands())
            command = by_name.get(name)
            if command is None:
                raise _InvalidParams(f"Mikser Studio has no tool “{name}”. List the tools again.")
            confirmation = command.get("confirmation")
            if confirmation and not command.get("readOnly"):
                refusal = await self._ask(str(confirmation))
                if refusal:
                    return _failure(refusal)
            outcome = await self._app.run(str(command["id"]), arguments)
        except AppUnavailable as error:
            return _failure(str(error))
        if isinstance(outcome, CommandError):
            return _failure(f"{outcome.message} ({outcome.code})")
        result: JsonObject = {"content": [{"type": "text", "text": json.dumps(outcome, ensure_ascii=False)}]}
        if self._protocol >= STRUCTURED_SINCE:
            result["structuredContent"] = outcome
        return result

    async def _ask(self, confirmation: str) -> str | None:
        """Asks the person, through the client, before a command that replaces their work. None when
        they agree; otherwise why the command did not run."""
        if not self._can_ask:
            return (
                f"This command needs the person's agreement (“{confirmation}”), and this MCP client cannot ask "
                "for it. Ask the person to run it in Mikser Studio instead."
            )
        ask_id = f"ask-{next(self._ids)}"
        answer: asyncio.Future[JsonObject] = asyncio.get_running_loop().create_future()
        self._asked[ask_id] = answer
        await self._send(
            {
                "jsonrpc": "2.0",
                "id": ask_id,
                "method": "elicitation/create",
                "params": {"message": confirmation, "requestedSchema": {"type": "object", "properties": {}}},
            }
        )
        if (await answer).get("action") == "accept":
            return None
        return "The person did not agree, so the command did not run."

    async def _error(self, request_id: Any, code: int, message: str) -> None:
        await self._send({"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}})


class _InvalidParams(Exception):
    pass


def _failure(message: str) -> JsonObject:
    return {"content": [{"type": "text", "text": message}], "isError": True}
