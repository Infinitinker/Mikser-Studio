"""Finding FFmpeg: never in the working directory, a configured path only when absolute, and ffprobe
next to ffmpeg.
"""

from pathlib import Path

import pytest

from mikserstudio_ai.backends.builtin.ffmpeg import Ffmpeg
from tests.support import make_program, same_file


def test_ffmpeg_is_never_found_in_the_working_directory(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    planted = tmp_path / "downloads"
    make_program(planted, "ffmpeg")
    monkeypatch.chdir(planted)
    monkeypatch.delenv("NoDefaultCurrentDirectoryInExePath", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    assert Ffmpeg(None).executable() is None


def test_a_configured_path_must_be_absolute(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    tools = make_program(tmp_path / "tools", "ffmpeg")
    monkeypatch.chdir(tmp_path)
    assert same_file(Ffmpeg(str(tools)).executable(), tools)
    assert Ffmpeg(str(Path("tools") / tools.name)).executable() is None


def test_ffprobe_is_taken_next_to_ffmpeg(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    ffmpeg = make_program(tmp_path / "tools", "ffmpeg")
    ffprobe = make_program(tmp_path / "tools", "ffprobe")
    make_program(tmp_path / "elsewhere", "ffprobe")
    monkeypatch.setenv("PATH", str(tmp_path / "elsewhere"))
    assert same_file(Ffmpeg(str(ffmpeg)).ffprobe(), ffprobe)
