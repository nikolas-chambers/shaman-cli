# Configuration

Layers, lowest priority first; objects merge key by key:

1. `~/.config/shaman/shaman.json(c)` (Windows: `%APPDATA%\shaman`)
2. `shaman.json(c)` or `.shaman/shaman.json(c)`, from the project root down to the current directory
3. `$SHAMAN_CONFIG` (a path), then `$SHAMAN_CONFIG_CONTENT` (inline JSON)

JSONC is accepted (comments, trailing commas). Any string may use `{env:NAME}` or `{file:path}`.
`shaman debug config` prints the sources and the merged result.

```jsonc
{
  "model": "opencode/big-pickle",           // provider/model; default: best free model ($SHAMAN_MODEL also works)
  "default_agent": "build",
  "free_fallback": true,                    // rate-limited free model -> next free model
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

Environment variables that change behaviour: `TAVILY_API_KEY`, `EXA_API_KEY` or `BRAVE_SEARCH_API_KEY` switch
`websearch` to that service (default DuckDuckGo). Shell commands always run with `CI=true`, `GIT_TERMINAL_PROMPT=0`,
`PAGER=cat` and similar set (unless you set them yourself), so tools fail fast instead of waiting for input.

Project memory: the `memory` tool writes `.shaman/MEMORY.md`, which is added to every session's system prompt. Edit
or delete it by hand any time.
