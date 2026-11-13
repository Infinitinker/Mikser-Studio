"""A small client for the parts of ComfyUI's HTTP API the backend uses: /system_stats, /models,
/object_info, /prompt, /ws, /history, /view and /queue (https://docs.comfy.org/development/comfyui-server/comms_routes).
"""

import json
from collections.abc import AsyncGenerator
from pathlib import Path
from typing import Any

import aiohttp

# Short, so a missing ComfyUI is noticed quickly when the backends are listed.
QUICK = aiohttp.ClientTimeout(total=5)
# Saved images can be large; reading one should still not take long on loopback.
DOWNLOAD = aiohttp.ClientTimeout(total=120)


class ComfyError(Exception):
    """ComfyUI answered with an error, or could not be reached."""


class ComfyClient:
    def __init__(self, base_url: str) -> None:
        self.base_url = base_url
        self._session: aiohttp.ClientSession | None = None

    async def version(self) -> str:
        data = await self._get_json("/system_stats")
        system = data.get("system")
        return str(system.get("comfyui_version", "unknown")) if isinstance(system, dict) else "unknown"

    async def models(self, folder: str) -> list[str]:
        """The model files ComfyUI has in one of its model folders, such as "vae"."""
        data = await self._get_json(f"/models/{folder}")
        return [str(name) for name in data] if isinstance(data, list) else []

    async def has_node(self, node: str) -> bool:
        data = await self._get_json(f"/object_info/{node}")
        return isinstance(data, dict) and node in data

    async def events(self, client_id: str) -> AsyncGenerator[dict[str, Any]]:
        """ComfyUI's messages to this client: progress, executing, execution_error and so on. Binary
        messages (previews) are skipped."""
        url = self.base_url.replace("http://", "ws://", 1).replace("https://", "wss://", 1)
        try:
            async with self._client().ws_connect(f"{url}/ws?clientId={client_id}") as socket:
                async for message in socket:
                    if message.type == aiohttp.WSMsgType.TEXT:
                        data = json.loads(message.data)
                        if isinstance(data, dict):
                            yield data
        except aiohttp.ClientError as error:
            raise ComfyError(f"the connection to ComfyUI failed ({error})") from error

    async def queue_prompt(self, workflow: dict[str, Any], client_id: str) -> str:
        """Queues a workflow in ComfyUI's API format; returns its prompt id."""
        try:
            async with self._client().post(
                f"{self.base_url}/prompt", json={"prompt": workflow, "client_id": client_id}, timeout=QUICK
            ) as response:
                data = await response.json(content_type=None)
                if response.status != 200:
                    raise ComfyError(_refusal(data, response.status))
        except (aiohttp.ClientError, TimeoutError, ValueError) as error:
            raise ComfyError(f"ComfyUI did not take the job ({str(error) or type(error).__name__})") from error
        if not isinstance(data, dict) or not isinstance(data.get("prompt_id"), str):
            raise ComfyError("ComfyUI took the job but gave it no id")
        return str(data["prompt_id"])

    async def history(self, prompt_id: str) -> dict[str, Any]:
        """The finished prompt's record: its outputs per node and its status; empty while it runs."""
        data = await self._get_json(f"/history/{prompt_id}")
        entry = data.get(prompt_id)
        return entry if isinstance(entry, dict) else {}

    async def download(self, image: dict[str, Any], target: Path) -> None:
        """Saves an image ComfyUI made, as /history lists it ({filename, subfolder, type}), to `target`."""
        params = {
            "filename": str(image.get("filename", "")),
            "subfolder": str(image.get("subfolder", "")),
            "type": str(image.get("type", "output")),
        }
        try:
            async with self._client().get(f"{self.base_url}/view", params=params, timeout=DOWNLOAD) as response:
                if response.status != 200:
                    raise ComfyError(f"ComfyUI could not send {params['filename']} (HTTP {response.status})")
                target.write_bytes(await response.read())
        except (aiohttp.ClientError, TimeoutError) as error:
            raise ComfyError(f"ComfyUI could not send {params['filename']} ({error})") from error

    async def delete_queued(self, prompt_id: str) -> None:
        """Takes a prompt that has not started off ComfyUI's queue. One that already runs goes on: the
        backend never interrupts ComfyUI, since that would stop whatever it is running, ours or not."""
        try:
            async with self._client().post(f"{self.base_url}/queue", json={"delete": [prompt_id]}, timeout=QUICK):
                pass
        except (aiohttp.ClientError, TimeoutError):
            pass  # ComfyUI is gone: nothing is left to take off.

    async def close(self) -> None:
        if self._session is not None:
            await self._session.close()
            self._session = None

    async def _get_json(self, path: str) -> Any:
        try:
            async with self._client().get(f"{self.base_url}{path}", timeout=QUICK) as response:
                if response.status != 200:
                    raise ComfyError(f"ComfyUI answered {path} with HTTP {response.status}")
                return await response.json(content_type=None)
        except (aiohttp.ClientError, TimeoutError, ValueError) as error:
            reason = str(error) or type(error).__name__
            raise ComfyError(f"ComfyUI is not reachable at {self.base_url} ({reason})") from error

    def _client(self) -> aiohttp.ClientSession:
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession()
        return self._session


def _refusal(data: Any, status: int) -> str:
    """Why ComfyUI refused a workflow, from its error and the first node error."""
    if not isinstance(data, dict):
        return f"ComfyUI refused the job (HTTP {status})"
    error = data.get("error")
    message = error.get("message", "") if isinstance(error, dict) else str(error or "")
    for node_error in (data.get("node_errors") or {}).values():
        for detail in node_error.get("errors") or []:
            message += f": {detail.get('message', '')} {detail.get('details', '')}".rstrip()
            return f"ComfyUI refused the job ({message})"
    return f"ComfyUI refused the job ({message or f'HTTP {status}'})"
