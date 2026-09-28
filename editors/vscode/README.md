# Shaman for VS Code

~ Your link between worlds ~, in your editor. The extension starts `shaman serve` for the open workspace and shows the
full shaman chat (inline diffs, revert and fork, modes, effort, themes) in the activity bar.

- **Shaman: Ask About Selection** (`Ctrl+Alt+S` / `Cmd+Alt+S`, or right-click): puts the selected code, with its file
  and line numbers, into the chat input.
- **Shaman: Add Current File to Chat**: adds `@path` so shaman reads the file.
- **Shaman: Open Chat in Browser**, **Shaman: Restart Server**.

Works in remote workspaces too (SSH, containers, Codespaces): the port is forwarded for you.

## Setup

1. Install shaman (`curl -fsSL https://raw.githubusercontent.com/nikolas-chambers/shaman-cli/main/scripts/install.sh | sh`,
   or the PowerShell one-liner on Windows) and set up a model (`shaman auth login <provider>`).
2. Install the extension: `code --install-extension shaman-vscode-0.1.0.vsix`, or from the marketplace once published.
3. Settings: `shaman.path` if shaman is not on your PATH; `shaman.extraArgs`, e.g. `["--mode", "acceptEdits"]`.

## Build

No build step: `npx @vscode/vsce package` in this folder produces `shaman-vscode-<version>.vsix`.
The server listens on 127.0.0.1 with a random token only this extension knows.
