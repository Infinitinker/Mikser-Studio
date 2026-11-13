"""The MCP bridge: a program of its own that offers Mikser Studio's commands to agents as MCP tools.

It speaks the Model Context Protocol on stdin and stdout and drives the running app only through its
local API: the `/v1` routes, the `/v1/events` socket and the discovery file `api.json`. Run it with
`python -m mikserstudio_ai.mcp`.
"""
