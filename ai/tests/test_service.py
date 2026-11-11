"""Runs the real sidecar process the way the Mikser Studio app does."""

import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

from mikserstudio_ai.service import READY_PREFIX


def test_sidecar_announces_its_port_serves_and_stops_when_stdin_closes(tmp_path: Path) -> None:
    process = subprocess.Popen(
        [
            sys.executable, "-m", "mikserstudio_ai", "--port", "0", "--cache-dir", str(tmp_path / "cache"),
            "--exit-with-stdin",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        env={**os.environ, "MIKSERSTUDIO_AI_TOKEN": "secret"},
        text=True,
    )  # fmt: skip
    try:
        assert process.stdout is not None and process.stdin is not None
        line = process.stdout.readline().strip()
        assert line.startswith(READY_PREFIX), line
        port = int(line.removeprefix(READY_PREFIX))

        with urllib.request.urlopen(f"http://127.0.0.1:{port}/v1/health", timeout=10) as response:
            assert json.load(response)["status"] == "ok"
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/v1/backends", headers={"Authorization": "Bearer secret"}
        )
        with urllib.request.urlopen(request, timeout=30) as response:
            assert {backend["id"] for backend in json.load(response)} >= {"builtin", "ollama"}

        process.stdin.close()
        assert process.wait(timeout=20) == 0
        assert process.stdout.read() == ""
        assert (tmp_path / "cache").is_dir()
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
