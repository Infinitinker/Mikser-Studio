"""Mikser Studio's commands as MCP tools: their names, descriptions, input schemas and hints."""

import logging
import re
from typing import Any

log = logging.getLogger("mikserstudio.mcp")

JsonObject = dict[str, Any]

# Tool names clients accept everywhere: letters, digits, "_" and "-", at most 64 of them.
_NOT_IN_NAMES = re.compile(r"[^A-Za-z0-9_-]")


def tool_name(command_id: str) -> str:
    """The tool name for a command: `project.import_media` becomes `project_import_media`."""
    return _NOT_IN_NAMES.sub("_", command_id)[:64]


def tool_for(command: JsonObject) -> JsonObject:
    """The MCP tool that runs `command`, a command spec as `GET /v1/commands` lists it."""
    command_id = str(command["id"])
    description = str(command.get("description") or command.get("title") or command_id)
    if command.get("undoable"):
        description += " It can be undone (edit_undo)."
    confirmation = command.get("confirmation")
    if confirmation:
        description += f" The person is asked before it runs: “{confirmation}”"
    schema = command.get("paramsSchema")
    if not isinstance(schema, dict) or schema.get("type") != "object":
        schema = {"type": "object", "properties": {}, "additionalProperties": False}
    read_only = bool(command.get("readOnly"))
    return {
        "name": tool_name(command_id),
        "title": str(command.get("title") or command_id),
        "description": description,
        "inputSchema": schema,
        "annotations": {
            "title": str(command.get("title") or command_id),
            "readOnlyHint": read_only,
            "destructiveHint": bool(confirmation) and not read_only,
            "openWorldHint": False,
        },
    }


def tools_for(commands: list[JsonObject]) -> tuple[list[JsonObject], dict[str, JsonObject]]:
    """The tools for `commands`, and the command each tool name runs. When two ids give the same
    name, the first one in the list keeps it."""
    tools: list[JsonObject] = []
    by_name: dict[str, JsonObject] = {}
    for command in commands:
        if not isinstance(command.get("id"), str):
            continue
        tool = tool_for(command)
        if tool["name"] in by_name:
            log.warning("Left out %s: its tool name is taken by %s.", command["id"], by_name[tool["name"]]["id"])
            continue
        by_name[tool["name"]] = command
        tools.append(tool)
    return tools, by_name
