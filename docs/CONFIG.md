# Configuration

Layers, lowest priority first; objects merge key by key:

1. `~/.config/shaman/shaman.json(c)` (Windows: `%APPDATA%\shaman`)
2. `shaman.json(c)` or `.shaman/shaman.json(c)`, from the project root down to the current directory
3. `$SHAMAN_CONFIG` (a path), then `$SHAMAN_CONFIG_CONTENT` (inline JSON)

JSONC is accepted (comments, trailing commas). Any string may use `{env:NAME}` or `{file:path}`.
`shaman debug config` prints the sources and the merged result.

```jsonc
{
  "model": "opencode/big-pickle",           // provider/model; default: picked from providers with a key ($SHAMAN_MODEL also works)
  "default_agent": "build",
  "free_fallback": true,                    // rate-limited no-cost model -> the next one
  "snapshot": true,                         // git-backed /undo of file changes
  "instructions": ["docs/CONVENTIONS.md"],  // SHAMAN.md or AGENTS.md load automatically

  // allow | ask | deny, per permission, optionally per subject pattern (most specific wins)
  "permission": {
    "edit": "ask",
    "bash": { "*": "ask", "npm test*": "allow", "rm -rf *": "deny" },
    "webfetch": "allow", "websearch": "allow", "mcp": "ask", "plugin": "ask", "external_directory": "ask"
  },

  "agent": {
    "plan": { "model": "anthropic/claude-sonnet-5" },
    "review": { "description": "Reviews diffs", "prompt": "Report bugs only.", "tools": { "write": false, "edit": false } }
  },

  "command": { "test": { "template": "Run the tests and fix failures. $ARGUMENTS", "description": "Fix tests" } },

  // Extra or overridden providers (OpenAI-compatible or Anthropic protocol)
  "provider": {
    "mylab": { "api": "openai", "baseURL": "http://gpu-box:8000/v1", "apiKey": "{env:LAB_KEY}",
               "models": { "qwen-coder": { "name": "Qwen Coder", "context": 128000 } } }
  },

  "mcp": {
    "fs":   { "command": ["npx", "-y", "@modelcontextprotocol/server-filesystem", "."], "environment": {} },
    "docs": { "url": "https://example.com/mcp", "headers": { "X-Team": "core" } },   // OAuth when the server asks
    "old":  { "command": ["legacy-server"], "enabled": false }
  },

  "lsp": { "timeout": 3000, "servers": { "zls": { "command": ["zls"], "extensions": [".zig"] }, "pyright": { "disabled": true } } },
  "formatter": { "ruff": false, "mine": { "command": ["my-fmt", "$FILE"], "extensions": [".foo"] } },   // or false
  "plugin": ["./tools/guard.py", { "command": ["node", "plugin.js"] }],
  "notify": { "desktop": true, "sound": true, "min_seconds": 20 },   // or false; turn done / permission / question
  "redact_secrets": true,          // mask API keys, tokens and private keys in tool output before the model sees it
  "goal_max_rounds": 5,            // /goal: how many self-checks before stopping
  "update_check": true,            // tell the TUI and web UI about new releases (daily; SHAMAN_NO_UPDATE_CHECK=1 also stops it)
  "share": { "url": "https://paste.example.com/api", "token": "{env:PASTE_TOKEN}" }   // optional upload for `shaman share`
}
```

Markdown agents (`.shaman/agents/<name>.md`) and commands (`.shaman/commands/<name>.md`) take front matter:

```markdown
---
description: Reviews diffs for bugs
mode: primary            # primary | subagent | all
model: opencode/big-pickle
tools: read, grep, glob, bash
---
You are a strict reviewer. Report bugs only, with path:line.
```

Command templates support `$ARGUMENTS`, `$1`..`$9`, `` !`shell` `` and `@file`.

Permission modes sit on top of the rules: `default` asks as configured, `acceptEdits` allows file edits inside the
project without asking, `plan` switches to the read-only plan agent (which ends with `plan_exit`, asking you to
approve the plan before it switches to `build`), and `yolo` allows every "ask". Explicit `deny` rules hold in every
mode. Set it with `--mode`, `/mode`, `Shift-Tab` in the terminal UI, the mode menu in the web UI, or `"mode"` in an
API message.

Environment variables that change behaviour: `TAVILY_API_KEY`, `EXA_API_KEY` or `BRAVE_SEARCH_API_KEY` switch
`websearch` to that service (default DuckDuckGo). Shell commands always run with `CI=true`, `GIT_TERMINAL_PROMPT=0`,
`PAGER=cat` and similar set (unless you set them yourself), so tools fail fast instead of waiting for input.

Project memory: the `memory` tool writes `.shaman/MEMORY.md`, which is added to every session's system prompt. Edit
or delete it by hand any time.

## MCP permissions

MCP tools ask first (permission `mcp`, subject `<server>_<tool>`). Allow a trusted server, or tune single tools, right
where the server is configured; general `permission` rules still win, and `deny` holds even with `--yolo`:

```jsonc
"mcp": {
  "docs":   { "url": "https://example.com/mcp", "permission": "allow" },
  "github": { "command": ["github-mcp"], "permission": { "*": "allow", "delete_repository": "deny", "merge_pull_request": "ask" } }
},
"permission": { "mcp": { "fs_write_*": "ask" } }
```

## Keys

Where a provider's key comes from, first match wins: `apiKey` in config (or `[keys]` in `shaman.ini`), a key saved
with `shaman auth login`, the provider's environment variable, then `apiKeyCommand`. Saved keys live in the OS
keychain when there is one: macOS Keychain, Windows Credential Manager, or the Secret Service on Linux (GNOME Keyring,
KWallet; needs `secret-tool` and a desktop session). `auth.json` then only notes that a key exists. Without a
keychain, in portable mode, or with `SHAMAN_NO_KEYCHAIN=1`, the key is stored in `auth.json` (readable only by you).
`shaman auth secure` moves keys from `auth.json` into the keychain; `shaman auth list` shows where each key comes from;
`shaman doctor` shows the storage in use.

## Terminal UI

```jsonc
"tui": {
  "theme": "halloween",      // shaman, dark, light, halloween, dracula, nord, gruvbox, solarized, monokai, sunset, ocean, forest, mono
  "accent": "#ff8c1a",       // optional colour overrides: accent, code, heading, add, del, warn, info
  "mouse": true,             // wheel scrolling (hold Shift to select text); or /mouse
  "statusline": "~/.config/shaman/status.sh",   // gets the session as JSON on stdin; its first line shows bottom right
  "keybinds": { "ctrl+g": "/sessions", "ctrl+t": "/theme", "ctrl+y": "copy" }  // a slash command or palette, editor,
                             // details, paste, copy, mode, clear, new
}
```

In the input: `!command` runs a shell command in the project (without asking, since you typed it) and sends its
output with your next message; `# note` appends to `.shaman/MEMORY.md`; Ctrl-V (or `/paste`) attaches an image from
the clipboard; `/copy` copies the last reply (`/copy code` its last code block); `/context` shows what the context
window is spent on; `/theme` switches theme; `/effort` sets reasoning effort; Shift-Tab cycles permission modes.

## Providers and models

Any provider, built in or your own, takes these options; a model entry for a known model changes only the fields
you give:

```jsonc
"provider": {
  "openai": {
    "models": {
      "gpt-5.5": { "reasoningEffort": "high", "context": { "limit": 64000, "compactAt": 0.8 } }
    }
  },
  "google": {
    "models": { "gemini-2.5-flash": { "temperature": 0.2, "tools": { "websearch": false, "webfetch": false } } }
  },
  "mylab": {
    "api": "openai",                 // openai (chat completions) | responses (OpenAI Responses) | anthropic
    "baseURL": "http://gpu-box:8000/v1",
    "apiKeyCommand": "pass show lab/key",   // or "apiKey"; the command's output is reused for 45 minutes
    "authHeader": "api-key",         // send the key in this header instead of Authorization: Bearer
    "headers": { "X-Team": "core" },
    "body": { "top_p": 0.9 },        // extra request fields for every model here
    "models": {
      "qwen-coder": { "name": "Qwen Coder", "output": 8192, "vision": false,
                      "cost": { "input": 0, "output": 0 }, "body": { "repetition_penalty": 1.05 },
                      "context": { "window": 32768, "maxToolOutput": 12000, "keepToolOutputs": 3 } }
    }
  }
}
```

| Model option | |
|---|---|
| `reasoningEffort` | `low`, `medium` or `high` for models that think (OpenAI `reasoning_effort` or Responses `reasoning`, Anthropic extended thinking, Gemini). Change it live with `/effort` or `--effort` (`off` disables thinking) |
| `temperature` | default temperature (an agent's own setting wins) |
| `body` | extra JSON merged into each request, for provider-specific knobs |
| `tools` | turn tools off for this model, e.g. `{"*": false, "read": true, "edit": true, "bash": true}` for a small model |
| `context` | a number (window size) or an object: `window`, `limit` (use at most this many tokens), `pruneAt` (default 0.6: stub out old tool outputs above this share), `compactAt` (0.85: summarise), `keepToolOutputs` (6), `maxToolOutput` (characters per tool result) |

A top-level `"context": {...}` sets the same knobs for every model, and `"reasoning_effort"` a default effort.
Thinking is kept across tool calls where the API needs it: signed Anthropic thinking blocks and encrypted
Responses reasoning items are sent back as they came.

Cloud platforms appear once their environment is set: Azure OpenAI (`AZURE_OPENAI_ENDPOINT`,
`AZURE_OPENAI_API_KEY`; model ids are deployment names), Google Vertex AI (`GOOGLE_CLOUD_PROJECT`, optional
`GOOGLE_CLOUD_LOCATION`, token from `gcloud auth print-access-token`) and Amazon Bedrock (`AWS_BEARER_TOKEN_BEDROCK`,
`AWS_REGION`). OpenAI itself uses the Responses API.

## Sandbox

`"sandbox": true` runs every `bash` command in an OS sandbox: it can read anything but write only inside the project
and the temp directory. `{"network": false}` also cuts network access; `{"writable": ["~/.cache"]}` adds paths.
Linux uses [bubblewrap](https://github.com/containers/bubblewrap) (`bwrap`), macOS `sandbox-exec`; Windows has no
sandbox yet. If it is on but unavailable, commands are refused, never run unsandboxed. Permission rules still apply
on top. `shaman doctor` shows the status.

## Hooks

Shell commands that run at points in the agent loop, for rules you want enforced every time rather than hoped for:

```jsonc
"hooks": {
  "PreToolUse":       [{ "matcher": "bash", "command": "./scripts/guard.sh" }],
  "PostToolUse":      [{ "matcher": "edit|write|multiedit", "command": "npx eslint --fix \"$SHAMAN_FILE\"", "timeout": 60 }],
  "UserPromptSubmit": [{ "command": "git status --short" }],
  "SessionStart":     [{ "command": "cat docs/STATUS.md" }],
  "Stop":             [{ "command": "make -s test >/dev/null 2>&1 || { echo 'tests are failing' >&2; exit 2; }" }],
  "Notification":     [{ "command": "notify-send shaman \"$(jq -r .message)\"" }]
}
```

| Event | When | Exit 2 (or `{"decision":"block","reason":...}` on stdout) |
|---|---|---|
| `PreToolUse` | before a tool runs | the call is refused; the reason goes to the model |
| `PostToolUse` | after a tool runs | the reason is appended to the tool output for the model |
| `UserPromptSubmit` | before your message is sent | the message is rejected. On exit 0, stdout is added as context |
| `SessionStart` | first message of a session | stdout is added as context |
| `Stop` | when the agent is about to finish | it keeps working with the reason as feedback (at most 3 times per turn) |
| `Notification` | when shaman needs your permission | (ignored) |

`matcher` is a case-insensitive regex over the tool name (empty or `*` matches all). Each command runs through the
shell in the project root with the event as JSON on stdin (`hook_event_name`, `session_id`, `cwd`, `tool_name`,
`tool_input`, `tool_response`, `prompt`, `last_assistant_message`) and `SHAMAN_PROJECT_DIR`, `SHAMAN_SESSION_ID`,
`SHAMAN_HOOK_EVENT`, `SHAMAN_TOOL` and `SHAMAN_FILE` set. Other exit codes are reported and ignored. The nested
`{"matcher", "hooks": [{"type": "command", "command"}]}` form is accepted too. `shaman --debug=tool` logs every hook
run; traces record stdin, stdout, stderr and exit codes.
