"""The "comfyui" backend: images made by the user's ComfyUI, through its HTTP API.

It runs workflows of its own in ComfyUI and never downloads models or installs nodes: when ComfyUI
lacks what a workflow needs, the backend is unavailable and says what is missing.
"""

from collections.abc import Mapping, Sequence
from typing import Any

from mikserstudio_ai.backend import Availability, Backend, BackendError, JobContext, Operation, Outcome, Settings
from mikserstudio_ai.backends.comfyui.client import ComfyClient, ComfyError
from mikserstudio_ai.backends.comfyui.text_to_image import OPERATION, generate_image, missing

# The folder in ComfyUI's output where the images go, one subfolder per job.
OUTPUT_FOLDER = "mikserstudio"


class ComfyBackend:
    def __init__(self, client: ComfyClient, output_folder: str = OUTPUT_FOLDER) -> None:
        self._client = client
        self._output_folder = output_folder

    @property
    def id(self) -> str:
        return "comfyui"

    @property
    def title(self) -> str:
        return "ComfyUI"

    def operations(self) -> Sequence[Operation]:
        return [OPERATION]

    async def availability(self) -> Availability:
        try:
            await self._client.version()
            lacking = await missing(self._client)
        except ComfyError:
            return Availability(
                False,
                f"ComfyUI is not running at {self._client.base_url}. Start ComfyUI (install it from "
                "https://www.comfy.org if needed) and try again.",
            )
        if lacking:
            return Availability(
                False,
                f"ComfyUI lacks {', '.join(lacking)}. Add them in ComfyUI, for example from its Qwen Image 2.1 "
                "template, and try again.",
            )
        return Availability(True)

    async def run(self, op: str, params: Mapping[str, Any], context: JobContext) -> Outcome:
        if op != OPERATION.id:
            raise BackendError(f"The comfyui backend does not offer “{op}”.")
        return await generate_image(self._client, params, context, self._output_folder)

    async def close(self) -> None:
        await self._client.close()


def create(settings: Settings) -> Backend:
    return ComfyBackend(ComfyClient(settings.comfyui_url))
