"""Mikser Studio AI sidecar: runs AI backends for Mikser Studio behind a local, token-protected HTTP API."""

from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("mikserstudio-ai")
except PackageNotFoundError:  # Running from a source tree that was never installed.
    __version__ = "0.0.0"
