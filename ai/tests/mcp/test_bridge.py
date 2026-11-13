"""The MCP bridge against a fake Mikser Studio: the tools it lists, running them, asking the person
before commands that replace their work, following the app as it starts and stops, and the program on
stdin and stdout.
"""

import asyncio
import contextlib
import json
import subprocess
import sys
from collections.abc import AsyncIterator
from pathlib import Path
from typing import Any

import aiohttp
import pytest
from aiohttp import web
from aiohttp.test_utils import TestServer
from pytest_aiohttp import AiohttpServer

from mikserstudio_ai.mcp import bridge as bridge_module
from mikserstudio_ai.mcp.app import NOT_RUNNING, App
from mikserstudio_ai.mcp.bridge import Bridge

TOKEN = "test-token"

COMMANDS: list[dict[str, Any]] = [
    {
        "id": "edit.undo",
        "title": "Undo",
        "description": "Reverts the last change.",
        "paramsSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        "undoable": False,
        "readOnly": False,
    },
    {
        "id": "project.import_media",
        "title": "Import media…",
        "description": "Adds a media file to the media bin.",
        "paramsSchema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]},
        "undoable": True,
        "readOnly": False,
    },
    {
        "id": "project.list",
        "title": "List project",
        "description": "Lists the media and the timeline.",
        "paramsSchema": {},
        "undoable": False,
        "readOnly": True,
    },
    {
        "id": "project.new",
        "title": "New project",
        "description": "Starts an empty project.",
        "paramsSchema": {},
        "undoable": False,
        "readOnly": False,
        "confirmation": "Start a new project? Unsaved changes will be lost.",
    },
    {
        "id": "project.save",
        "title": "Save project as…",
        "description": "Saves the project to a .msproj file.",
        "paramsSchema": {
            "type": "object",
            "properties": {"path": {"type": "string"}, "overwrite": {"type": "boolean", "default": False}},
            "required": ["path"],
            "additionalProperties": False,
        },
        "undoable": False,
        "readOnly": False,
        "confirmation": "",
        "confirmationParam": "overwrite",
    },
]


class FakeStudio:
    """The parts of Mikser Studio's local API the bridge uses."""

    def __init__(self) -> None:
        self.commands = [dict(command) for command in COMMANDS]
        self.runs: list[tuple[str, dict[str, Any]]] = []
        self.sockets: list[web.WebSocketResponse] = []
        self.socket_opened = asyncio.Event()

    def app(self) -> web.Application:
        app = web.Application()
        app.router.add_get("/v1/commands", self.list_commands)
        app.router.add_post("/v1/commands/{id}", self.run)
        app.router.add_get("/v1/events", self.events)
        return app

    @staticmethod
    def authorized(request: web.Request) -> bool:
        return request.headers.get("Authorization") == f"Bearer {TOKEN}"

    async def list_commands(self, request: web.Request) -> web.Response:
        if not self.authorized(request):
            return web.json_response({"error": {"code": "unauthorized", "message": "No token."}}, status=401)
        return web.json_response(self.commands)

    async def run(self, request: web.Request) -> web.Response:
        if not self.authorized(request):
            return web.json_response({"error": {"code": "unauthorized", "message": "No token."}}, status=401)
        command_id = request.match_info["id"]
        params = await request.json()
        self.runs.append((command_id, params))
        if command_id == "edit.undo":
            message = "There is nothing to undo."
            return web.json_response({"error": {"code": "not_available", "message": message}}, status=409)
        if command_id == "project.save" and not params.get("overwrite"):
            message = f"“{params['path']}” already exists. Replace it with this project?"
            return web.json_response({"error": {"code": "needs_confirmation", "message": message}}, status=409)
        return web.json_response({"result": {"ran": command_id}})

    async def events(self, request: web.Request) -> web.WebSocketResponse:
        socket = web.WebSocketResponse()
        if not self.authorized(request):
            raise web.HTTPUnauthorized()
        await socket.prepare(request)
        self.sockets.append(socket)
        self.socket_opened.set()
        async for _ in socket:
            pass
        return socket

    async def close_sockets(self) -> None:
        """What the app does when it stops: it closes its event sockets as going away."""
        for socket in self.sockets:
            await socket.close(code=aiohttp.WSCloseCode.GOING_AWAY)

    async def send_event(self, event: dict[str, Any]) -> None:
        for socket in self.sockets:
            await socket.send_json(event)


def write_discovery(path: Path, server: TestServer) -> None:
    base_url = str(server.make_url("")).rstrip("/")
    path.write_text(
        json.dumps(
            {
                "apiVersion": 1,
                "baseUrl": base_url,
                "eventsUrl": base_url.replace("http://", "ws://") + "/v1/events",
                "pid": 1,
                "port": server.port,
                "token": TOKEN,
            }
        ),
        encoding="utf-8",
    )


class Client:
    """An MCP client talking to a Bridge: requests and their answers, and what the bridge sends it."""

    def __init__(self, session: aiohttp.ClientSession, discovery_path: Path) -> None:
        self.sent: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.bridge = Bridge(App(session, discovery_path), self._receive)
        self._ids = 0

    async def _receive(self, message: dict[str, Any]) -> None:
        await self.sent.put(message)

    async def send(self, message: dict[str, Any]) -> None:
        await self.bridge.receive(json.dumps(message).encode())

    async def next_message(self) -> dict[str, Any]:
        return await asyncio.wait_for(self.sent.get(), timeout=5)

    async def request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        self._ids += 1
        await self.send({"jsonrpc": "2.0", "id": self._ids, "method": method, "params": params or {}})
        message = await self.next_message()
        assert message.get("id") == self._ids, message
        return message

    async def start(self, capabilities: dict[str, Any] | None = None, version: str = "2025-06-18") -> None:
        answer = await self.request(
            "initialize",
            {"protocolVersion": version, "capabilities": capabilities or {}, "clientInfo": {"name": "test"}},
        )
        assert "result" in answer
        await self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    async def tools(self) -> dict[str, dict[str, Any]]:
        listed = (await self.request("tools/list"))["result"]["tools"]
        return {tool["name"]: tool for tool in listed}

    async def call(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        result: dict[str, Any] = (await self.request("tools/call", {"name": name, "arguments": arguments or {}}))[
            "result"
        ]
        return result


@pytest.fixture
async def studio(aiohttp_server: AiohttpServer, tmp_path: Path) -> AsyncIterator[tuple[FakeStudio, Path]]:
    fake = FakeStudio()
    server: TestServer = await aiohttp_server(fake.app())
    discovery_path = tmp_path / "api.json"
    write_discovery(discovery_path, server)
    yield fake, discovery_path


@pytest.fixture
async def session() -> AsyncIterator[aiohttp.ClientSession]:
    async with aiohttp.ClientSession() as client_session:
        yield client_session


async def test_initialize_offers_tools_that_can_change(session: aiohttp.ClientSession, tmp_path: Path) -> None:
    client = Client(session, tmp_path / "api.json")
    answer = await client.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
    result = answer["result"]
    assert result["protocolVersion"] == "2025-06-18"
    assert result["capabilities"] == {"tools": {"listChanged": True}}
    assert result["serverInfo"]["name"] == "mikser-studio"
    assert "project_list" in result["instructions"]


async def test_initialize_answers_an_unknown_version_with_the_newest(
    session: aiohttp.ClientSession, tmp_path: Path
) -> None:
    client = Client(session, tmp_path / "api.json")
    answer = await client.request("initialize", {"protocolVersion": "1999-01-01", "capabilities": {}})
    assert answer["result"]["protocolVersion"] == bridge_module.PROTOCOL_VERSIONS[0]


async def test_commands_are_listed_as_tools_with_their_hints(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    _, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    tools = await client.tools()
    assert set(tools) == {"edit_undo", "project_import_media", "project_list", "project_new", "project_save"}

    listing = tools["project_list"]
    assert listing["annotations"]["readOnlyHint"] is True
    assert listing["annotations"]["destructiveHint"] is False
    assert listing["inputSchema"] == {"type": "object", "properties": {}, "additionalProperties": False}

    imported = tools["project_import_media"]
    assert imported["title"] == "Import media…"
    assert imported["inputSchema"]["required"] == ["path"]
    assert "can be undone" in imported["description"]

    new = tools["project_new"]
    assert new["annotations"]["destructiveHint"] is True
    assert "Unsaved changes will be lost" in new["description"]

    # Only the person's agreement gives overwrite, so the agent never sees it.
    save = tools["project_save"]
    assert set(save["inputSchema"]["properties"]) == {"path"}
    assert save["annotations"]["destructiveHint"] is True
    assert "the person is asked first" in save["description"]


async def test_a_tool_runs_its_command_with_the_arguments(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    fake, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    result = await client.call("project_import_media", {"path": "D:/clip.mp4"})
    assert fake.runs == [("project.import_media", {"path": "D:/clip.mp4"})]
    assert result["structuredContent"] == {"ran": "project.import_media"}
    assert json.loads(result["content"][0]["text"]) == {"ran": "project.import_media"}
    assert "isError" not in result


async def test_older_clients_get_the_result_as_text_only(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    _, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start(version="2025-03-26")
    result = await client.call("project_list")
    assert "structuredContent" not in result
    assert json.loads(result["content"][0]["text"]) == {"ran": "project.list"}


async def test_a_failed_command_is_a_tool_error_with_the_apps_message(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    _, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    result = await client.call("edit_undo")
    assert result["isError"] is True
    assert result["content"][0]["text"] == "There is nothing to undo. (not_available)"


async def test_an_unknown_tool_is_an_invalid_request(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    _, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    answer = await client.request("tools/call", {"name": "project_delete_everything", "arguments": {}})
    assert answer["error"]["code"] == bridge_module.INVALID_PARAMS
    assert "List the tools again" in answer["error"]["message"]


async def test_without_the_app_there_are_no_tools_and_calls_say_to_start_it(
    session: aiohttp.ClientSession, tmp_path: Path
) -> None:
    client = Client(session, tmp_path / "api.json")
    await client.start()
    assert await client.tools() == {}
    result = await client.call("project_list")
    assert result["isError"] is True
    assert result["content"][0]["text"] == NOT_RUNNING


async def test_a_command_with_a_confirmation_does_not_run_when_the_client_cannot_ask(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    fake, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    result = await client.call("project_new")
    assert result["isError"] is True
    assert "Start a new project? Unsaved changes will be lost." in result["content"][0]["text"]
    assert fake.runs == []


@pytest.mark.parametrize(("action", "runs"), [("accept", 1), ("decline", 0), ("cancel", 0)])
async def test_a_command_with_a_confirmation_runs_only_when_the_person_agrees(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path], action: str, runs: int
) -> None:
    fake, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start(capabilities={"elicitation": {}})
    await client.send({"jsonrpc": "2.0", "id": 99, "method": "tools/call", "params": {"name": "project_new"}})

    asked = await client.next_message()
    assert asked["method"] == "elicitation/create"
    assert asked["params"]["message"] == "Start a new project? Unsaved changes will be lost."
    assert fake.runs == []
    await client.send({"jsonrpc": "2.0", "id": asked["id"], "result": {"action": action}})

    answer = await client.next_message()
    assert answer["id"] == 99
    assert len(fake.runs) == runs
    assert answer["result"].get("isError", False) is (runs == 0)


@pytest.mark.parametrize(("action", "replaced"), [("accept", True), ("decline", False)])
async def test_a_run_that_needs_agreement_asks_the_person_and_runs_again_only_when_they_agree(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path], action: str, replaced: bool
) -> None:
    fake, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start(capabilities={"elicitation": {}})
    # The agent's own overwrite is dropped: only the person's answer gives it.
    arguments = {"path": "C:/projects/first.msproj", "overwrite": True}
    call = {"name": "project_save", "arguments": arguments}
    await client.send({"jsonrpc": "2.0", "id": 98, "method": "tools/call", "params": call})

    asked = await client.next_message()
    assert asked["method"] == "elicitation/create"
    assert asked["params"]["message"] == "“C:/projects/first.msproj” already exists. Replace it with this project?"
    await client.send({"jsonrpc": "2.0", "id": asked["id"], "result": {"action": action}})

    answer = await client.next_message()
    assert answer["id"] == 98
    assert answer["result"].get("isError", False) is not replaced
    expected: list[tuple[str, dict[str, Any]]] = [("project.save", {"path": "C:/projects/first.msproj"})]
    if replaced:
        expected.append(("project.save", {"path": "C:/projects/first.msproj", "overwrite": True}))
    assert fake.runs == expected


async def test_a_run_that_needs_agreement_fails_when_the_client_cannot_ask(
    session: aiohttp.ClientSession, studio: tuple[FakeStudio, Path]
) -> None:
    fake, discovery_path = studio
    client = Client(session, discovery_path)
    await client.start()
    result = await client.call("project_save", {"path": "C:/projects/first.msproj"})
    assert result["isError"] is True
    assert "already exists" in result["content"][0]["text"]
    assert len(fake.runs) == 1


async def test_the_client_hears_when_the_app_starts_stops_and_changes_its_commands(
    aiohttp_server: AiohttpServer, session: aiohttp.ClientSession, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(bridge_module, "RETRY_SECONDS", 0.05)
    discovery_path = tmp_path / "api.json"
    client = Client(session, discovery_path)
    await client.start()
    watcher = asyncio.create_task(client.bridge.watch_app())
    try:
        fake = FakeStudio()
        server: TestServer = await aiohttp_server(fake.app())
        write_discovery(discovery_path, server)
        assert (await client.next_message())["method"] == "notifications/tools/list_changed"

        await asyncio.wait_for(fake.socket_opened.wait(), timeout=5)
        fake.commands.append({"id": "hello.greet", "title": "Greet", "paramsSchema": {}})
        await fake.send_event({"type": "command.registered", "id": "hello.greet"})
        assert (await client.next_message())["method"] == "notifications/tools/list_changed"
        assert "hello_greet" in await client.tools()

        discovery_path.unlink()
        await fake.close_sockets()
        await server.close()
        assert (await client.next_message())["method"] == "notifications/tools/list_changed"
    finally:
        watcher.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await watcher


async def test_unknown_methods_and_broken_messages_get_json_rpc_errors(
    session: aiohttp.ClientSession, tmp_path: Path
) -> None:
    client = Client(session, tmp_path / "api.json")
    answer = await client.request("resources/list")
    assert answer["error"]["code"] == bridge_module.METHOD_NOT_FOUND
    await client.bridge.receive(b"{not json")
    assert (await client.next_message())["error"]["code"] == bridge_module.PARSE_ERROR


def test_the_program_speaks_mcp_on_stdin_and_stdout_and_ends_with_its_input(tmp_path: Path) -> None:
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18"}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    ]
    stdin = "".join(json.dumps(request) + "\n" for request in requests)
    finished = subprocess.run(
        [sys.executable, "-m", "mikserstudio_ai.mcp", "--discovery-file", str(tmp_path / "api.json")],
        input=stdin.encode(),
        capture_output=True,
        timeout=30,
        check=False,
    )
    assert finished.returncode == 0, finished.stderr.decode()
    answers = [json.loads(line) for line in finished.stdout.decode().splitlines()]
    assert [answer["id"] for answer in answers] == [1, 2]
    assert answers[1]["result"] == {"tools": []}
    assert "Looking for Mikser Studio" in finished.stderr.decode()
