"""Tells the service when to stop: SIGTERM/SIGINT where the OS has them, or stdin being closed.

Closing stdin is how the Mikser Studio app stops the sidecar on every OS: Windows has no SIGTERM for console
processes, and a closed pipe also means the app is gone, even if it crashed. If the service has not
stopped STOP_GRACE_SECONDS later (a job hung the event loop), the process exits anyway, so it never
outlives the app.
"""

import asyncio
import logging
import os
import signal
import sys
import threading
import time

STOP_GRACE_SECONDS = 10.0

logger = logging.getLogger(__name__)


def stop_on_signals(stop: asyncio.Event) -> None:
    loop = asyncio.get_running_loop()
    for number in (signal.SIGTERM, signal.SIGINT):
        try:
            loop.add_signal_handler(number, stop.set)
        except NotImplementedError:  # Windows: Ctrl+C raises KeyboardInterrupt instead.
            return


def stop_when_stdin_closes(stop: asyncio.Event) -> None:
    loop = asyncio.get_running_loop()

    def wait_for_eof() -> None:
        stream = sys.stdin.buffer
        while stream.read(4096):
            pass
        logger.info("stdin was closed; stopping")
        loop.call_soon_threadsafe(stop.set)
        time.sleep(STOP_GRACE_SECONDS)
        logger.error("The service did not stop within %.0f seconds; exiting now", STOP_GRACE_SECONDS)
        os._exit(1)

    threading.Thread(target=wait_for_eof, name="stdin-watch", daemon=True).start()
