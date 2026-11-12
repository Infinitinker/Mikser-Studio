"""The command line of the sidecar."""

import argparse
import os
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

TOKEN_ENVIRONMENT = "MIKSERSTUDIO_AI_TOKEN"
LOOPBACK_HOSTS = frozenset({"127.0.0.1", "::1", "localhost"})


@dataclass(frozen=True, slots=True)
class Options:
    host: str
    port: int
    token: str
    cache_dir: Path
    ffmpeg: str | None
    ollama_url: str
    exit_with_stdin: bool
    log_level: str


def parse(argv: Sequence[str] | None = None, environment: dict[str, str] | None = None) -> Options:
    """Parses argv; exits with a usage message (status 2) when it is wrong, like every argparse tool."""
    env = os.environ if environment is None else environment
    parser = _parser()
    args = parser.parse_args(argv)
    token = args.token or env.get(TOKEN_ENVIRONMENT, "")
    if not token:
        parser.error(f"pass the API token with --token or the {TOKEN_ENVIRONMENT} environment variable")
    if args.host not in LOOPBACK_HOSTS:
        parser.error("--host must be a loopback address (127.0.0.1, ::1 or localhost); the API is local only")
    if not 0 <= args.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    return Options(
        host=args.host,
        port=args.port,
        token=token,
        cache_dir=Path(args.cache_dir).resolve(),
        ffmpeg=args.ffmpeg,
        ollama_url=args.ollama_url.rstrip("/"),
        exit_with_stdin=args.exit_with_stdin,
        log_level=args.log_level,
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="mikserstudio-ai",
        description=(
            'AI sidecar of Mikser Studio. Prints "MIKSERSTUDIO_AI_READY port=<port>" on stdout once it listens.'
        ),
    )
    parser.add_argument("--host", default="127.0.0.1", help="loopback address to listen on (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=0, help="port to listen on; 0 picks a free one (default)")
    parser.add_argument("--token", help=f"API token clients must send; defaults to ${TOKEN_ENVIRONMENT}")
    parser.add_argument("--cache-dir", required=True, help="folder for job outputs")
    parser.add_argument("--ffmpeg", help="path of ffmpeg; found on PATH when omitted")
    parser.add_argument("--ollama-url", default="http://127.0.0.1:11434", help="base URL of the Ollama server")
    parser.add_argument(
        "--exit-with-stdin", action="store_true", help="stop when stdin is closed (used by the Mikser Studio app)"
    )
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    return parser
