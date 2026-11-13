"""Runs the API until told to stop, then cancels the jobs and closes the backends."""

import asyncio
import logging
import socket

from aiohttp import web

from mikserstudio_ai.arguments import Options
from mikserstudio_ai.backend import Settings
from mikserstudio_ai.jobs import JobManager
from mikserstudio_ai.registry import BackendRegistry
from mikserstudio_ai.server import create_app
from mikserstudio_ai.stop_signals import stop_on_signals, stop_when_stdin_closes

logger = logging.getLogger(__name__)

READY_PREFIX = "MIKSERSTUDIO_AI_READY port="


async def serve(options: Options) -> None:
    options.cache_dir.mkdir(parents=True, exist_ok=True)
    settings = Settings(
        cache_dir=options.cache_dir,
        ffmpeg=options.ffmpeg,
        ollama_url=options.ollama_url,
        comfyui_url=options.comfyui_url,
    )
    registry = BackendRegistry.from_entry_points(settings)
    jobs = JobManager(options.cache_dir)
    runner = web.AppRunner(create_app(registry, jobs, options.token), access_log=None)
    await runner.setup()
    try:
        site = web.TCPSite(runner, options.host, options.port)
        await site.start()
        port = _bound_port(runner)
        stop = asyncio.Event()
        stop_on_signals(stop)
        if options.exit_with_stdin:
            stop_when_stdin_closes(stop)
        logger.info(
            "Listening on %s:%d with the backends %s; job outputs go to %s",
            options.host,
            port,
            ", ".join(backend.id for backend in registry.all()) or "(none)",
            options.cache_dir,
        )
        print(f"{READY_PREFIX}{port}", flush=True)
        await stop.wait()
    finally:
        await jobs.close()
        await runner.cleanup()
        await registry.close()
        logger.info("Stopped")


def _bound_port(runner: web.AppRunner) -> int:
    for address in runner.addresses:
        if isinstance(address, tuple) and len(address) >= 2:
            return int(address[1])
    raise OSError(socket.EAI_NONAME, "The server is not listening on any TCP address")
