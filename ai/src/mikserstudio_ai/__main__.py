"""Entry point: mikserstudio-ai --cache-dir <dir> [--host 127.0.0.1] [--port 0] [--token <t>] ..."""

import asyncio
import logging
import sys
from collections.abc import Sequence

from mikserstudio_ai.arguments import parse
from mikserstudio_ai.logs import log_to_stderr
from mikserstudio_ai.service import serve

logger = logging.getLogger("mikserstudio_ai")


def main(argv: Sequence[str] | None = None) -> int:
    options = parse(argv)
    log_to_stderr(options.log_level)
    try:
        asyncio.run(serve(options))
    except KeyboardInterrupt:
        logger.info("Interrupted")
    except OSError as error:
        logger.error("The AI service cannot listen on %s:%d: %s", options.host, options.port, error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
