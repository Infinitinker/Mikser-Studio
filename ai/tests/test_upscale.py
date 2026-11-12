import asyncio
import json
import shutil
import subprocess
from pathlib import Path

import pytest

from mikserstudio_ai.backend import BackendError, JobContext, Settings
from mikserstudio_ai.backends.builtin import create

pytestmark = pytest.mark.skipif(
    shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None, reason="ffmpeg/ffprobe not on PATH"
)


def make_clip(path: Path, size: str, seconds: float, rate: int = 10) -> Path:
    subprocess.run(
        [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", f"testsrc=size={size}:rate={rate}:duration={seconds}",
            "-f", "lavfi", "-i", f"sine=frequency=440:duration={seconds}",
            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", str(path),
        ],
        check=True,
    )  # fmt: skip
    return path


def probe(path: Path) -> dict[str, object]:
    completed = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "stream=codec_type,width,height", "-of", "json", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    result: dict[str, object] = json.loads(completed.stdout)
    return result


class Recorder:
    def __init__(self) -> None:
        self.reports: list[tuple[float, str]] = []

    def __call__(self, fraction: float, message: str) -> None:
        self.reports.append((fraction, message))


def context(tmp_path: Path, recorder: Recorder) -> JobContext:
    output_dir = tmp_path / "job"
    output_dir.mkdir(exist_ok=True)
    return JobContext("job-test", output_dir, recorder)


@pytest.fixture(scope="module")
def tiny_clip(tmp_path_factory: pytest.TempPathFactory) -> Path:
    return make_clip(tmp_path_factory.mktemp("clips") / "tiny.mp4", "64x36", 1)


async def test_backend_is_available_with_ffmpeg(tmp_path: Path) -> None:
    backend = create(Settings(cache_dir=tmp_path))
    state = await backend.availability()
    assert state.available, state.reason


async def test_backend_explains_a_missing_ffmpeg(tmp_path: Path) -> None:
    backend = create(Settings(cache_dir=tmp_path, ffmpeg=str(tmp_path / "missing-ffmpeg")))
    state = await backend.availability()
    assert not state.available
    assert "--ffmpeg" in state.reason


async def test_upscale_doubles_the_size_and_keeps_the_audio(tmp_path: Path, tiny_clip: Path) -> None:
    recorder = Recorder()
    backend = create(Settings(cache_dir=tmp_path))
    outcome = await backend.run("video.upscale", {"input": str(tiny_clip), "scale": 2}, context(tmp_path, recorder))

    [output] = outcome.outputs
    assert output.kind == "video"
    assert output.path.name == "tiny_x2.mkv"
    streams = {stream["codec_type"]: stream for stream in probe(output.path)["streams"]}  # type: ignore[attr-defined]
    assert (streams["video"]["width"], streams["video"]["height"]) == (128, 72)
    assert "audio" in streams
    assert outcome.data == {"width": 128, "height": 72}
    assert outcome.provenance["tools"]["ffmpeg"]
    assert outcome.provenance["method"]["ai"] is False
    assert "lanczos" in outcome.provenance["method"]["filter"]
    assert recorder.reports[-1][0] == 1.0
    assert all(0.0 <= fraction <= 1.0 for fraction, _ in recorder.reports)


async def test_upscale_refuses_a_missing_input(tmp_path: Path) -> None:
    backend = create(Settings(cache_dir=tmp_path))
    with pytest.raises(BackendError, match="does not exist"):
        await backend.run(
            "video.upscale", {"input": str(tmp_path / "gone.mp4"), "scale": 2}, context(tmp_path, Recorder())
        )


async def test_upscale_refuses_outputs_larger_than_8k(tmp_path: Path) -> None:
    large = make_clip(tmp_path / "large.mp4", "2400x1400", 0.1)
    backend = create(Settings(cache_dir=tmp_path))
    with pytest.raises(BackendError, match="larger than 8K"):
        await backend.run("video.upscale", {"input": str(large), "scale": 4}, context(tmp_path, Recorder()))


async def test_canceling_stops_ffmpeg_and_removes_the_partial_output(tmp_path: Path) -> None:
    long_clip = make_clip(tmp_path / "long.mp4", "320x180", 20, rate=30)
    recorder = Recorder()
    job_context = context(tmp_path, recorder)
    backend = create(Settings(cache_dir=tmp_path))
    task = asyncio.create_task(backend.run("video.upscale", {"input": str(long_clip), "scale": 4}, job_context))
    async with asyncio.timeout(30):
        while not any(0.0 < fraction < 1.0 for fraction, _ in recorder.reports):
            assert not task.done(), "the upscale finished before it could be canceled"
            await asyncio.sleep(0.02)
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert list(job_context.output_dir.iterdir()) == []
