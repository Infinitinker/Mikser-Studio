"""Finding programs: never in the working directory, only in absolute PATH folders or a configured absolute
path, and ffprobe next to ffmpeg.
"""

import os
import sys
from pathlib import Path

import pytest

from mikserstudio_ai.backends.builtin.ffmpeg import Ffmpeg
from mikserstudio_ai.processes import find_program


def make_program(folder: Path, name: str) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (f"{name}.exe" if sys.platform == "win32" else name)
    path.write_bytes(b"")
    path.chmod(0o755)
    return path


def same_file(found: str | None, expected: Path) -> bool:
    return found is not None and os.path.samefile(found, expected)


def test_a_program_in_the_working_directory_is_never_found(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    planted = tmp_path / "downloads"
    make_program(planted, "ffmpeg")
    monkeypatch.chdir(planted)
    monkeypatch.delenv("NoDefaultCurrentDirectoryInExePath", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    assert find_program("ffmpeg") is None
    assert Ffmpeg(None).executable() is None


def test_programs_are_found_in_absolute_path_folders_only(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    tools = make_program(tmp_path / "tools", "ffmpeg")
    make_program(tmp_path / "relative", "ffmpeg")
    monkeypatch.chdir(tmp_path)
    assert find_program("ffmpeg", "relative") is None
    assert same_file(find_program("ffmpeg", os.pathsep.join(["relative", "", str(tmp_path / "tools")])), tools)


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
