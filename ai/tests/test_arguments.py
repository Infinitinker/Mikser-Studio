"""The sidecar's command line: defaults, the session token from the command line or the environment, and
exit with usage on bad arguments.
"""

from pathlib import Path

import pytest

from mikserstudio_ai.arguments import TOKEN_ENVIRONMENT, parse


def test_defaults(tmp_path: Path) -> None:
    options = parse(["--token", "t", "--cache-dir", str(tmp_path)], environment={})
    assert options.host == "127.0.0.1"
    assert options.port == 0
    assert options.token == "t"
    assert options.cache_dir == tmp_path.resolve()
    assert options.ffmpeg is None
    assert options.ollama_url == "http://127.0.0.1:11434"
    assert options.comfyui_url == "http://127.0.0.1:8188"
    assert options.exit_with_stdin is False


def test_token_can_come_from_the_environment(tmp_path: Path) -> None:
    options = parse(["--cache-dir", str(tmp_path)], environment={TOKEN_ENVIRONMENT: "secret"})
    assert options.token == "secret"


def test_options_are_read(tmp_path: Path) -> None:
    options = parse(
        [
            "--token", "t", "--cache-dir", str(tmp_path), "--port", "5000", "--host", "::1",
            "--ffmpeg", "C:/ffmpeg/ffmpeg.exe", "--ollama-url", "http://127.0.0.1:9999/", "--exit-with-stdin",
            "--comfyui-url", "http://127.0.0.1:8190/",
        ],
        environment={},
    )  # fmt: skip
    assert (options.host, options.port, options.ffmpeg) == ("::1", 5000, "C:/ffmpeg/ffmpeg.exe")
    assert options.ollama_url == "http://127.0.0.1:9999"
    assert options.comfyui_url == "http://127.0.0.1:8190"
    assert options.exit_with_stdin is True


@pytest.mark.parametrize(
    "argv",
    [
        ["--cache-dir", "x"],
        ["--token", "t"],
        ["--token", "t", "--cache-dir", "x", "--host", "0.0.0.0"],
        ["--token", "t", "--cache-dir", "x", "--port", "70000"],
    ],
)
def test_bad_command_lines_exit_with_usage(argv: list[str]) -> None:
    with pytest.raises(SystemExit) as raised:
        parse(argv, environment={})
    assert raised.value.code == 2
