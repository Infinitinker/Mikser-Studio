<p align="center">
  <img src="docs/images/icon.png" alt="Mikser Studio icon: three colored mixer faders on a dark rounded tile" width="128">
</p>

<h1 align="center">Mikser Studio</h1>

<p align="center">
  <b>A multimedia studio you share with AI agents.</b><br>
  Create and edit video, sound, images and more, with the AI tools you choose, local or online.
</p>

<p align="center">
  <a href="https://github.com/Infinitinker/Mikser-Studio/releases/latest"><img src="https://img.shields.io/github/v/release/Infinitinker/Mikser-Studio" alt="Latest release"></a>
  <a href="https://github.com/Infinitinker/Mikser-Studio/actions/workflows/checks.yml"><img src="https://img.shields.io/github/actions/workflow/status/Infinitinker/Mikser-Studio/checks.yml?branch=main&label=checks" alt="Checks on main"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
  <img src="https://img.shields.io/badge/platform-Windows-555" alt="Platform: Windows">
  <img src="https://img.shields.io/badge/works-offline-2ea44f" alt="Works offline">
</p>

Mikser Studio is a desktop studio for video and sound. You put clips on a timeline, watch them in GPU-drawn monitors and arrange the panels the way you work, and the AI tools on your own computer work on it with you. It is in early development: the editing core, the panels, the local API, plugins and the first AI features are there, and much more is planned.

## What it does

- 🎬 **Plays and edits video and sound** on a timeline with video and audio tracks, with a source monitor for one media item and a program monitor for the whole timeline, both drawn on the GPU.
- 🪟 **Lets you arrange the studio**: every panel can be moved, docked, undocked and stacked into tabs, the arrangement is kept for next time, and you can save it as a workspace of its own.
- 🤖 **Brings AI into your project**: generate images with [ComfyUI](https://www.comfy.org) and text with [Ollama](https://ollama.com), the AI tools on your own computer. Images come into the media bin with a record of how they were made, and a slow or stopped tool never holds up the editor.
- ⌨️ **Does everything through commands**: menus, shortcuts and the command palette (**Ctrl+K**) run the same commands, and every change can be undone.
- 🧩 **Grows with plugins**: a plugin adds commands, settings and panels without changing Mikser Studio itself.
- 🔌 **Can be driven from outside**: other programs and scripts on your computer can run its commands through a local API, and AI agents such as Claude Code through the MCP bridge that comes with it.
- 🔒 **Stays on your computer**: it works offline, and your media and the AI's work never leave your computer.

## Getting started

Mikser Studio has no release yet. Once it has one:

1. **Download** `MikserStudio-<version>-windows-x64-setup.exe` from the [latest release](https://github.com/Infinitinker/Mikser-Studio/releases/latest) and run it.
2. **Start Mikser Studio** from the installer's last page or the Start menu.
3. **Drop a video or sound file** on the media bin, drag it onto the timeline, and play it in the program monitor (**Space**).

Until then, Mikser Studio runs from source, as [docs/development.md](docs/development.md) describes; `start.bat` starts it from a built checkout.

## Questions

**Where are my settings?**
On Windows, in `%LOCALAPPDATA%\Mikser Studio`: your workspaces and settings, the cache with the AI's results in `cache`, the logs in `logs`, and `api.json`, which tells other programs on your computer how to reach the local API while Mikser Studio runs. They stay when you update Mikser Studio.

**Do I need Ollama or ComfyUI?**
Only for AI: Ollama to generate text, and ComfyUI with the Qwen Image 2.1 models to generate images. Mikser Studio connects to them on your computer; the AI commands `ai.settings` and `ai.set_settings` show and change their addresses.

**Windows says "Windows protected your PC".**
Mikser Studio is not code-signed yet, so Windows SmartScreen doesn't know it. Choose **More info**, then **Run anyway**.

## Contributing

Plugins, fixes and ideas are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md). Found a security problem? Please report it privately, as [SECURITY.md](SECURITY.md) describes.

Further documentation:

- [docs/architecture.md](docs/architecture.md): how Mikser Studio is built
- [docs/plugins.md](docs/plugins.md): writing a plugin
- [docs/ai.md](docs/ai.md): how Mikser Studio runs AI jobs in the AI tools you run
- [docs/api.md](docs/api.md): the local API, through which other programs control Mikser Studio
- [docs/development.md](docs/development.md): setting up from source, running it, and the checks and tests
- [docs/releasing.md](docs/releasing.md): how a release is built and published

## Support

Mikser Studio is free, with no ads and no tracking. If it helps you make things, you can buy me a coffee:

<p>
  <a href="https://ko-fi.com/infinitinker"><img src="https://img.shields.io/badge/Ko--fi-support-FF5E5B?logo=kofi&logoColor=white" alt="Support on Ko-fi"></a>
  <a href="https://buycoffee.to/infinitinker"><img src="https://img.shields.io/badge/buycoffee.to-support-FFB400" alt="Support on buycoffee.to"></a>
</p>

Ko-fi takes cards and PayPal; buycoffee.to suits Polish supporters, with BLIK and Polish bank transfers.

## License

Copyright (c) 2026 Infinitinker. Mikser Studio is free software under the GNU General Public License, version 3 or any later version (GPL-3.0-or-later); see [LICENSE](LICENSE).

The licenses of the libraries a release ships with, such as Qt, MLT and FFmpeg, are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which comes with every release as `THIRD-PARTY-NOTICES.txt`.

Mikser Studio is not affiliated with or endorsed by the makers of the software it works with, such as Ollama, FFmpeg, MLT or Qt.
