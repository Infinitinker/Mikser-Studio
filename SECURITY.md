# Security

Mikser Studio opens media files from anywhere, loads native plugins, runs AI models in a helper process and serves a local API that other programs on the computer can reach, so security problems in it matter. Thank you for reporting them.

## Reporting a vulnerability

Please do not open a public issue. Report it privately through GitHub instead: open the repository's **Security** tab and choose **Report a vulnerability**. Describe what is affected, how to reproduce it, and what an attacker could do with it.

## What is in scope

- The local API on `127.0.0.1` and its discovery file `api.json`, including what a web page or another program on the same computer can make it do.
- The AI service (`ai/`) and its API, which the app starts on `127.0.0.1` with a session token.
- Project files (`.mikser`) and media files: anything a crafted file can make Mikser Studio do.
- Plugins: how they are found, loaded and given access to the app.

## Supported versions

Mikser Studio has not had a release yet; fixes go into the latest version on the main branch.
