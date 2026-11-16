# Contributing to Mikser Studio

  <!-- shared-start: the text up to shared-end is the same in every project; change it only where all projects change together. -->
- **Found a security problem?** Report it privately, as [SECURITY.md](SECURITY.md) describes, not in an issue.
  <!-- shared-end -->
- **Found a bug?** Open an issue with the bug report form, with the steps that lead to it.
- **Want Mikser Studio to work with another AI tool or service?** Ask for a connector with the
  connector request form.

Everything in the project is written in English, with American spelling (color, canceled, license).

## Contributing a connector

A connector lets Mikser Studio use another AI tool or service, on your computer or online. To ask
for one, open an issue with the connector request form, and say which API the tool speaks, with a
link to its docs. A shared, open API, such as the OpenAI-compatible one many tools speak, is
preferred over a tool's own: one connector then serves every tool that speaks it. How to write a
connector comes with the first code.

## Code

The developer docs come with the first code: `docs/development.md`, with the setup and the checks,
and `docs/architecture.md`, with how the code is organized. This guide links them once they exist.

<!-- shared-start: the text up to shared-end is the same in every project; change it only where all projects change together. -->
1. Work on a branch from `dev`, and open the pull request to `dev`. `main` gets the changes from `dev` through a pull request.
   <!-- shared-end -->
2. Run the checks before you push.
3. Add no dependency without checking its license first: everything Mikser Studio ships with must
   be compatible with GPL-3.0-or-later.
4. Put tests beside the code they test.

<!-- shared-start: the text up to shared-end is the same in every project; change it only where all projects change together. -->

Update the docs in the same pull request as the code they describe. Commits follow [Conventional Commits](https://www.conventionalcommits.org/), with a scope from CLAUDE.md.

<!-- shared-end -->
