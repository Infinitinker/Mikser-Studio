"""`python -m mikserstudio_ai.mcp`: the MCP bridge on stdin and stdout, one JSON-RPC message a line."""

import argparse
import asyncio
import contextlib
import json
import logging
import os
import sys
import threading
from collections.abc import Sequence
from pathlib import Path
from typing import Any, BinaryIO

import aiohttp

from mikserstudio_ai.mcp.app import App
from mikserstudio_ai.mcp.bridge import Bridge
from mikserstudio_ai.mcp.discovery import INSTANCE_NAMES, discovery_file


def parse_arguments(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m mikserstudio_ai.mcp",
        description="Offers the commands of a running Mikser Studio to an MCP client, such as Claude Code, "
        "as tools. It speaks MCP on stdin and stdout and finds the app through its discovery file.",
    )
    parser.add_argument(
        "--instance",
        choices=sorted(INSTANCE_NAMES),
        default=os.environ.get("MIKSERSTUDIO_INSTANCE")
        if os.environ.get("MIKSERSTUDIO_INSTANCE") in INSTANCE_NAMES
        else "installed",
        help="Which Mikser Studio to drive: the installed app, Mikser Studio Dev, built from source, or "
        "Mikser Studio E2E. The default is MIKSERSTUDIO_INSTANCE when it names one, otherwise the installed app.",
    )
    parser.add_argument("--discovery-file", type=Path, help="The app's api.json, when it is somewhere else.")
    return parser.parse_args(argv)


def read_lines(source: BinaryIO, loop: asyncio.AbstractEventLoop, lines: asyncio.Queue[bytes | None]) -> None:
    """Hands each line of `source` to the event loop, then None at its end. Runs on a thread of its
    own: a blocking read works on every system and for pipes and consoles alike."""
    for line in source:
        if line.strip():
            loop.call_soon_threadsafe(lines.put_nowait, line)
    loop.call_soon_threadsafe(lines.put_nowait, None)


async def serve(discovery_path: Path, source: BinaryIO, sink: BinaryIO) -> None:
    """Runs the bridge until the client closes `source`."""
    lock = asyncio.Lock()

    async def send(message: dict[str, Any]) -> None:
        async with lock:
            sink.write(json.dumps(message, ensure_ascii=False).encode() + b"\n")
            sink.flush()

    lines: asyncio.Queue[bytes | None] = asyncio.Queue()
    threading.Thread(target=read_lines, args=(source, asyncio.get_running_loop(), lines), daemon=True).start()
    async with aiohttp.ClientSession() as session:
        bridge = Bridge(App(session, discovery_path), send)
        watcher = asyncio.create_task(bridge.watch_app())
        try:
            while (line := await lines.get()) is not None:
                await bridge.receive(line)
        finally:
            watcher.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await watcher
            await bridge.close()


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    # stdout carries the protocol, so the log goes to stderr, where MCP clients collect it.
    logging.basicConfig(level=logging.INFO, stream=sys.stderr, format="%(levelname)s %(name)s: %(message)s")
    path = arguments.discovery_file or discovery_file(arguments.instance)
    logging.getLogger("mikserstudio.mcp").info("Looking for Mikser Studio through %s.", path)
    asyncio.run(serve(path, sys.stdin.buffer, sys.stdout.buffer))
    return 0


if __name__ == "__main__":
    sys.exit(main())
