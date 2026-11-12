"""Finding programs: never in the working directory, only in absolute PATH folders."""

import os
from pathlib import Path

import pytest

from mikserstudio_ai.processes import find_program
from tests.support import make_program, same_file


def test_a_program_in_the_working_directory_is_never_found(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    planted = tmp_path / "downloads"
    make_program(planted, "ffmpeg")
    monkeypatch.chdir(planted)
    monkeypatch.delenv("NoDefaultCurrentDirectoryInExePath", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path / "empty"))
    assert find_program("ffmpeg") is None


def test_programs_are_found_in_absolute_path_folders_only(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    tools = make_program(tmp_path / "tools", "ffmpeg")
    make_program(tmp_path / "relative", "ffmpeg")
    monkeypatch.chdir(tmp_path)
    assert find_program("ffmpeg", "relative") is None
    assert same_file(find_program("ffmpeg", os.pathsep.join(["relative", "", str(tmp_path / "tools")])), tools)
