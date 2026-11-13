"""image.generate: a still image from a prompt, made by Qwen Image 2.1 in the user's ComfyUI.

The workflow is ComfyUI's own text-to-image template for Qwen Image 2.1, without its prompt enhancer:
the diffusion model through its cache, the Qwen 3 VL text encoder, an empty latent, KSampler (euler,
simple, cfg 1), the VAE, and SaveImage into a folder of Mikser Studio's own in ComfyUI's output.
"""

import asyncio
import contextlib
import secrets
import uuid
from collections.abc import AsyncIterator, Mapping
from pathlib import Path, PurePosixPath
from typing import Any, TypedDict

from mikserstudio_ai.backend import BackendError, JobContext, Operation, Outcome, Output
from mikserstudio_ai.backends.comfyui.client import ComfyClient, ComfyError

DIFFUSION_MODEL = "qwen_image_2.1_int8_convrot.safetensors"
TEXT_ENCODER = "qwen3vl_8b_int8_convrot.safetensors"
VAE = "qwen_image_2.1_vae_bf16.safetensors"
# What the workflow needs from ComfyUI: (model folder, file), and the nodes that are not in every ComfyUI.
MODELS = (("diffusion_models", DIFFUSION_MODEL), ("text_encoders", TEXT_ENCODER), ("vae", VAE))
NODES = ("QwenImage21Cache", "TextEncodeQwenImage21")

SAVE_NODE = "9"
# What ComfyUI is doing while it runs each node of the workflow.
STAGES = {
    "1": "Loading the image model",
    "3": "Loading the text encoder",
    "4": "Reading the prompt",
    "7": "Loading the VAE",
    "8": "Decoding the image",
    SAVE_NODE: "Saving the image",
}

OPERATION = Operation(
    id="image.generate",
    title="Generate image",
    description="Makes a still image from a prompt with Qwen Image 2.1 in ComfyUI.",
    params_schema={
        "type": "object",
        "properties": {
            "prompt": {"type": "string", "minLength": 1, "description": "What the image shows."},
            "negative_prompt": {"type": "string", "default": "", "description": "What the image should not show."},
            "width": {
                "type": "integer",
                "minimum": 256,
                "maximum": 2048,
                "default": 1024,
                "description": "In pixels, rounded down to a multiple of 16.",
            },
            "height": {
                "type": "integer",
                "minimum": 256,
                "maximum": 2048,
                "default": 1024,
                "description": "In pixels, rounded down to a multiple of 16.",
            },
            "steps": {"type": "integer", "minimum": 1, "maximum": 100, "default": 25},
            "seed": {
                "type": "integer",
                "minimum": 0,
                "maximum": 2**48,
                "description": "The noise seed; a new one each time when omitted. The provenance keeps it.",
            },
        },
        "required": ["prompt"],
        "additionalProperties": False,
    },
)


class ImageResult(TypedDict):
    """The seed the image was made with, so the same image can be made again."""

    seed: int


def workflow(params: Mapping[str, Any], seed: int, filename_prefix: str) -> dict[str, Any]:
    """The workflow in ComfyUI's API format: node id -> {class_type, inputs}."""
    return {
        "1": {"class_type": "UNETLoader", "inputs": {"unet_name": DIFFUSION_MODEL, "weight_dtype": "default"}},
        "2": {"class_type": "QwenImage21Cache", "inputs": {"model": ["1", 0], "device": "auto", "dtype": "default"}},
        "3": {"class_type": "CLIPLoader", "inputs": {"clip_name": TEXT_ENCODER, "type": "qwen_image"}},
        "4": {
            "class_type": "TextEncodeQwenImage21",
            "inputs": {
                "clip": ["3", 0],
                "prompt": str(params["prompt"]),
                "negative_prompt": str(params.get("negative_prompt", "")),
                "resolution": 1024,
            },
        },
        "5": {
            "class_type": "EmptyLatentImage",
            "inputs": {"width": _size(params["width"]), "height": _size(params["height"]), "batch_size": 1},
        },
        "6": {
            "class_type": "KSampler",
            "inputs": {
                "model": ["2", 0],
                "seed": seed,
                "steps": int(params["steps"]),
                "cfg": 1.0,
                "sampler_name": "euler",
                "scheduler": "simple",
                "positive": ["4", 0],
                "negative": ["4", 1],
                "latent_image": ["5", 0],
                "denoise": 1.0,
            },
        },
        "7": {"class_type": "VAELoader", "inputs": {"vae_name": VAE}},
        "8": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["7", 0]}},
        SAVE_NODE: {"class_type": "SaveImage", "inputs": {"images": ["8", 0], "filename_prefix": filename_prefix}},
    }


async def missing(client: ComfyClient) -> list[str]:
    """What the workflow needs that this ComfyUI lacks: model files and nodes."""
    lacking = [name for folder, name in MODELS if name not in await client.models(folder)]
    lacking += [f"the node {node}" for node in NODES if not await client.has_node(node)]
    return lacking


async def generate_image(
    client: ComfyClient, params: Mapping[str, Any], context: JobContext, output_folder: str
) -> Outcome:
    seed = int(params["seed"]) if "seed" in params else secrets.randbelow(2**48)
    client_id = uuid.uuid4().hex
    try:
        version = await client.version()
        events = client.events(client_id)
        await anext(events)  # ComfyUI greets each socket with its queue's status: it is open.
        prompt_id = await client.queue_prompt(workflow(params, seed, f"{output_folder}/{context.job_id}"), client_id)
    except ComfyError as error:
        raise BackendError(f"{error}. Start ComfyUI and try again.") from error

    context.report(0.0, "Waiting in ComfyUI's queue")
    try:
        await _follow(events, prompt_id, context)
        images = (await client.history(prompt_id)).get("outputs", {}).get(SAVE_NODE, {}).get("images") or []
        if not images:
            raise BackendError("ComfyUI finished without saving an image. Look at ComfyUI's log for why.")
        outputs = []
        for index, image in enumerate(images, start=1):
            suffix = PurePosixPath(str(image.get("filename", ""))).suffix or ".png"
            target = context.output_dir / f"image-{index}{suffix}"
            await client.download(image, target)
            outputs.append(Output(Path(target), "image"))
    except asyncio.CancelledError:
        await client.delete_queued(prompt_id)
        raise
    except ComfyError as error:
        raise BackendError(f"{error}. Check that ComfyUI is still running and try again.") from error
    finally:
        with contextlib.suppress(Exception):
            await events.aclose()

    context.report(1.0, "Done")
    return Outcome(
        outputs=outputs,
        provenance={
            "tools": {"comfyui": version},
            "model": {"name": DIFFUSION_MODEL, "textEncoder": TEXT_ENCODER, "vae": VAE},
            "method": {
                "workflow": "Qwen Image 2.1 text to image",
                "sampler": "euler",
                "scheduler": "simple",
                "cfg": 1.0,
                "steps": int(params["steps"]),
                "seed": seed,
                "width": _size(params["width"]),
                "height": _size(params["height"]),
                "promptId": prompt_id,
            },
        },
        data=ImageResult(seed=seed),
    )


async def _follow(events: AsyncIterator[dict[str, Any]], prompt_id: str, context: JobContext) -> None:
    """Reports the prompt's progress until ComfyUI has finished it; raises BackendError when it failed."""
    done = 0.0  # Progress never goes back when a stage after the sampling starts.
    async for event in events:
        data = event.get("data")
        if not isinstance(data, dict) or data.get("prompt_id") != prompt_id:
            continue
        kind = event.get("type")
        if kind == "progress" and data.get("max"):
            value, total = int(data.get("value", 0)), int(data["max"])
            done = max(done, min(value / total, 1.0) * 0.95)
            context.report(done, f"Step {value} of {total}")
        elif kind == "executing" and data.get("node") is None or kind == "execution_success":
            return
        elif kind == "executing" and str(data.get("node")) in STAGES:
            context.report(done, STAGES[str(data["node"])])
        elif kind == "execution_error":
            message = data.get("exception_message") or "an error"
            raise BackendError(f"ComfyUI stopped with {str(message).strip()} in {data.get('node_type', 'a node')}.")
        elif kind == "execution_interrupted":
            raise BackendError("The job was stopped in ComfyUI.")
    raise BackendError("ComfyUI closed the connection before the image was done. Check that it is still running.")


def _size(pixels: object) -> int:
    """A width or height as the model takes it: a multiple of 16."""
    return int(str(pixels)) // 16 * 16
