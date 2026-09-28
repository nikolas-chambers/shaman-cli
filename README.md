# shaman-cli

A coding agent for your terminal, written in C++. A clean-room rewrite of the
[opencode](https://github.com/anomalyco/opencode) CLI: one native binary, instant startup, no runtime.

Works out of the box with free models. No account, no API key.

```sh
shaman                              # interactive session
shaman run "why is the build failing?"
git diff | shaman run "review this"
```

## Build

Requires CMake 3.24+, a C++23 compiler (GCC 13, Clang 17, MSVC 19.38) and libcurl.
nlohmann/json is used if installed, otherwise fetched.

```sh
# Debian/Ubuntu: apt install cmake ninja-build libcurl4-openssl-dev nlohmann-json3-dev
# macOS:         brew install cmake ninja curl nlohmann-json
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

## Free models

Shaman uses the free tier of [OpenCode Zen](https://opencode.ai/zen), which serves a rotating set of models at
no cost with the shared key `public`. That's the same mechanism opencode uses.

| Model | ID |
|---|---|
| Big Pickle (default) | `opencode/big-pickle` |
| Space Bunny | `opencode/space-bunny-free` |
| Nemotron 3 Ultra | `opencode/nemotron-3-ultra-free` |
| LongCat 2.5 Preview | `opencode/longcat-2.5-preview-free` |
| MiMo V2.6 Flash / V2.5 | `opencode/mimo-v2.6-flash-free`, `opencode/mimo-v2.5-free` |
| Ling 3.0 Flash Fin | `opencode/ling-3.0-flash-fin-free` |
| Nemotron 3.5 Lightning | `opencode/nemotron-3.5-lightning-free` |

The lineup changes. `shaman models --refresh` pulls the live list. If a free model is rate limited, shaman retries,
then moves to the next free model automatically (`"free_fallback": false` turns this off).

Free-tier providers may use prompts to improve their models. Don't send anything you wouldn't share.

## Usage

```
shaman [options]                 interactive session
shaman run [options] <message>   one-shot; also reads piped stdin
shaman models [--refresh]        list models
shaman agents                    list agents
shaman sessions                  list sessions for this project
shaman debug <what>              inspect internals (see Debugging)

-m, --model <provider/model>     -a, --agent <name>
-c, --continue                   -s, --session <id>
--format json                    --yolo (allow every tool call)
--reasoning                      --no-mcp
```

Interactive commands: `/new /sessions /agent /model /models /undo /compact /todos /cost /exit`.
`Ctrl-C` stops the current turn.

## Agents

| Agent | Mode | Purpose |
|---|---|---|
| `build` | primary | Default. All tools; edits and commands ask first. |
| `plan` | primary | Read-only: explores and proposes a plan, never edits. |
| `explore` | subagent | Fast read-only search, called via the `task` tool. |
| `general` | subagent | Multi-step delegated work. |

Add your own as `.shaman/agents/<name>.md` (project) or `~/.config/shaman/agents/<name>.md`:

```markdown
---
description: Reviews diffs for bugs
mode: primary
tools: read, grep, glob, bash
---
You are a strict code reviewer. Report bugs only, with path:line.
```

## Tools

`read` `write` `edit` `list` `glob` `grep` `bash` `webfetch` `todowrite` `todoread` `task`, plus any tools
from MCP servers. `grep` and `glob` use ripgrep when it's installed.

## Config

`shaman.json` (or `.jsonc`) in the project, at `~/.config/shaman/shaman.json`, in `$SHAMAN_CONFIG`, or inline in
`$SHAMAN_CONFIG_CONTENT`. Later layers win. Strings support `{env:NAME}` and `{file:path}`.

```jsonc
{
  "model": "opencode/big-pickle",
  "default_agent": "build",
  "instructions": ["docs/CONVENTIONS.md"],     // AGENTS.md / SHAMAN.md / CLAUDE.md load automatically
  "permission": {
    "edit": "ask",
    "bash": { "*": "ask", "npm test*": "allow", "git push*": "deny" },
    "webfetch": "allow"
  },
  "agent": { "plan": { "model": "opencode/space-bunny-free" } },
  "mcp": {
    "fs": { "command": ["npx", "-y", "@modelcontextprotocol/server-filesystem", "."] }
  }
}
```

Permissions are `allow`, `ask` or `deny`, per tool, optionally per command/path/URL pattern. The most specific
pattern wins. Defaults: reading is allowed, edits and shell commands ask, read-only commands like `git status`
are pre-approved.

## Debugging

See what the shaman is doing at every layer:

```sh
shaman --debug run "..."                     # everything, to stderr
shaman --debug=provider,tool run "..."       # just some categories
SHAMAN_DEBUG=http,sse shaman                 # same, via env
shaman --debug --log-file shaman.log         # to a file
shaman --trace run.jsonl run "..."           # every request, stream chunk, tool call and
                                             # permission decision as JSONL
```

Categories: `config provider http sse tool permission session agent mcp`.

Inspect how it's wired without running a model:

```sh
shaman debug paths          # config/data/cache/session locations
shaman debug config         # config sources and the merged result
shaman debug models         # resolved default model and fallback chain
shaman debug agents         # agents, their tools and permission overrides
shaman debug tools          # tool names, descriptions and JSON schemas
shaman debug prompt [agent] # the exact system prompt sent to the model
shaman debug permission bash "git push"   # how a rule evaluates
shaman debug session [id]   # a session's metadata and full message log
```

`SHAMAN_ZEN_URL` points shaman at another endpoint, such as the bundled mock server.

## Testing

```sh
ctest --test-dir build --output-on-failure   # unit + end-to-end
tests/e2e/run.sh build/shaman                # end-to-end only
python3 tests/mock/mock_zen.py --port 8765   # fake model server for manual runs:
SHAMAN_ZEN_URL=http://127.0.0.1:8765 shaman run "read README.md"
```

The mock speaks the streaming chat-completions protocol and scripts tool calls, rate limits and loops, so the
whole agent loop is tested offline.

## What's different from opencode

- **Native binary.** No Bun or Node runtime. Starts in milliseconds.
- **Safer defaults.** Edits and non-read-only shell commands ask first; `rm -rf /` is denied outright; tools refuse
  to edit files that haven't been read; leaving the project directory asks.
- **Free-model fallback.** A rate-limited free model hands off to the next one instead of failing the turn.
- **Loop guard.** Three identical tool calls in a row pause for confirmation.
- **Crash-safe sessions.** Append-only JSONL per session; a crash loses at most one line.
- **Undo without touching your repo.** Snapshots live in a private git dir; your index and branches are never used.
- **Debuggability built in.** Categorised debug logs, full JSONL traces and `shaman debug` introspection.
- **Offline test suite.** A mock model server drives end-to-end tests of the real binary.

## Status

Early. The core agent is complete and tested on Linux. macOS should work; the Windows code paths compile in
principle but haven't been built or tested yet, and MCP servers aren't supported on Windows yet.

Implemented: agent loop with streaming, the 11 tools above, agents and subagents, layered permissions, sessions
(continue, list), undo, auto-compaction, MCP (stdio), project instructions, config layering, free Zen models with
fallback, JSON output, debugging and tracing.

Not yet:

- [ ] Full-screen TUI (the interactive mode is line-based for now)
- [ ] Key-based providers: Anthropic, OpenAI, OpenRouter, Gemini, local models, and `shaman auth`
- [ ] Plugin SDK (hooks on events, tool calls and permissions); MCP covers custom tools today
- [ ] Headless server / HTTP API and client SDK
- [ ] LSP diagnostics after edits
- [ ] `apply_patch`, `websearch`, `skill` tools; custom slash commands
- [ ] Image and file attachments
- [ ] Session share, export/import, stats
- [ ] GitHub integration, ACP, self-update
- [ ] Remote MCP (HTTP/SSE) and OAuth

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it fits together and where each of these plugs in.

## License

MIT. See [LICENSE](LICENSE).

---

Shaman is inspired by and modelled on [opencode](https://github.com/anomalyco/opencode) by the opencode team (MIT).
Free models are provided by [OpenCode Zen](https://opencode.ai/zen). This project is not affiliated with opencode.
