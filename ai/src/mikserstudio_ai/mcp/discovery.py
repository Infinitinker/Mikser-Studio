"""Where the running Mikser Studio publishes its local API: the discovery file `api.json`."""

import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path

# The instances the app knows (`MIKSERSTUDIO_INSTANCE`) and the names their folders take.
INSTANCE_NAMES = {
    "installed": "Mikser Studio",
    "dev": "Mikser Studio Dev",
    "e2e": "Mikser Studio E2E",
}


@dataclass(frozen=True)
class Discovery:
    """What the discovery file says: where the API listens and the session token it needs."""

    base_url: str
    events_url: str
    token: str


def data_folder(instance: str) -> Path:
    """The instance's data folder, where Qt's AppLocalDataLocation puts it (docs/api.md)."""
    if override := os.environ.get("MIKSERSTUDIO_DATA_DIR"):
        return Path(override)
    name = INSTANCE_NAMES[instance]
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA")
        return Path(local) / name if local else Path.home() / "AppData" / "Local" / name
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / name
    xdg = os.environ.get("XDG_DATA_HOME")
    return (Path(xdg) if xdg else Path.home() / ".local" / "share") / name


def discovery_file(instance: str) -> Path:
    return data_folder(instance) / "api.json"


def read_discovery(path: Path) -> Discovery | None:
    """The discovery file's contents, or None while there is no usable file (the app is not running)."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or data.get("apiVersion") != 1:
        return None
    base_url, events_url, token = data.get("baseUrl"), data.get("eventsUrl"), data.get("token")
    if not (isinstance(base_url, str) and isinstance(events_url, str) and isinstance(token, str)):
        return None
    return Discovery(base_url=base_url, events_url=events_url, token=token)
