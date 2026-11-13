"""The Python side of Mikser Studio: the MCP bridge (`python -m mikserstudio_ai.mcp`)."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("mikserstudio-ai")
except PackageNotFoundError:  # Running from a source tree that was never installed.
    __version__ = "0.0.0"
