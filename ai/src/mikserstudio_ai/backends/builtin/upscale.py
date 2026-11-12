"""video.upscale with ffmpeg's lanczos scaler.

This is NOT an AI upscaler: it is a placeholder that exercises the whole pipeline (job queue,
progress, cancellation, cache, provenance, import into the project) until a ComfyUI backend offers
a real AI video.upscale. Lanczos only interpolates pixels; it adds no detail.
"""

from collections.abc import Mapping
from pathlib import Path
from typing import Any

from mikserstudio_ai.backend import BackendError, JobContext, Operation, Outcome, Output
from mikserstudio_ai.backends.builtin.ffmpeg import Ffmpeg

ENCODER = "libx264"
CRF = 16
# The largest frame the H.264 levels allow (8K UHD fits); bigger outputs fail to encode.
MAX_OUTPUT_PIXELS = 8192 * 4320

OPERATION = Operation(
    id="video.upscale",
    title="Upscale video (Lanczos, not AI)",
    description=(
        "Scales a video up 2, 3 or 4 times with ffmpeg's lanczos filter. A non-AI placeholder until an AI "
        "upscaler backend is installed."
    ),
    params_schema={
        "type": "object",
        "properties": {
            "input": {"type": "string", "minLength": 1, "description": "Absolute path of the video to upscale."},
            "scale": {"type": "integer", "enum": [2, 3, 4], "default": 2, "description": "Scale factor."},
        },
        "required": ["input"],
        "additionalProperties": False,
    },
)


async def upscale(ffmpeg: Ffmpeg, params: Mapping[str, Any], context: JobContext) -> Outcome:
    source = Path(params["input"])
    scale = int(params["scale"])
    if not source.is_file():
        raise BackendError(f"The video “{source}” does not exist. It may have been moved or deleted.")

    context.report(0.0, "Reading the video")
    info = await ffmpeg.probe_video(source)
    width, height = info.width * scale, info.height * scale
    if width * height > MAX_OUTPUT_PIXELS:
        raise BackendError(
            f"Upscaling {info.width}x{info.height} by {scale} gives {width}x{height}, larger than 8K. "
            "Choose a smaller scale."
        )

    output = context.output_dir / f"{source.stem}_x{scale}.mkv"
    video_filter = f"scale=iw*{scale}:ih*{scale}:flags=lanczos"
    context.report(0.0, f"Upscaling {info.width}x{info.height} to {width}x{height}")
    try:
        await ffmpeg.run(
            [
                "-y",
                "-i",
                str(source),
                "-map",
                "0:v:0",
                "-map",
                "0:a?",
                "-vf",
                video_filter,
                "-c:v",
                ENCODER,
                "-preset",
                "medium",
                "-crf",
                str(CRF),
                "-c:a",
                "copy",
                str(output),
            ],  # fmt: skip
            info.duration,
            lambda fraction: context.report(fraction, f"Upscaling {info.width}x{info.height} to {width}x{height}"),
        )
    except BaseException:
        output.unlink(missing_ok=True)
        raise

    return Outcome(
        outputs=[Output(output, "video")],
        provenance={
            "tools": {"ffmpeg": await ffmpeg.version()},
            "method": {
                "ai": False,
                "filter": video_filter,
                "encoder": f"{ENCODER} crf {CRF}",
                "note": "Lanczos scaling placeholder, not an AI model.",
            },
        },
        data={"width": width, "height": height},
    )
