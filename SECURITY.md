# Security

Mikser Studio opens media files from anywhere, runs Lua plugins, connects to the AI tools the user runs and serves a local API that other programs on the computer can reach, so security problems in it matter. Thank you for reporting them.

## Reporting a vulnerability

Please do not open a public issue. Report it privately through GitHub instead: open the repository's **Security** tab and choose **Report a vulnerability**. Describe what is affected, how to reproduce it, and what an attacker could do with it.

## What is in scope

- The local API on `127.0.0.1` and its discovery file `api.json`, including what a web page or another program on the same computer can make it do.
- The connections to the AI tools the user runs (Ollama through the Ollama plugin and the app's HTTP job kinds, ComfyUI through the AI engine), and what their answers can make Mikser Studio do.
- The MCP bridge, which offers the app's commands to agents through the local API.
- Project files (`.msproj`) and media files: anything a crafted file can make Mikser Studio do.
- Plugins: how they are found and turned on, the Lua sandbox they run in (each in a state and thread of its own, without files, processes or bytecode, with limits on memory and run time), the permissions that give them access to the app, and the network entries that limit where their requests may go.

## Supported versions

Mikser Studio has not had a release yet; fixes go into the latest version on the main branch.
