"""Logging goes to stderr: stdout is reserved for the single READY line the Mikser Studio app waits for."""

import logging
import sys

FORMAT = "%(asctime)s %(levelname)s %(name)s: %(message)s"


def log_to_stderr(level: str) -> None:
    logging.basicConfig(level=level, format=FORMAT, stream=sys.stderr)
    # One line per API request would drown the log: the app polls the jobs twice a second.
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)
