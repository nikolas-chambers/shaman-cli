# shaman-cli

**Homepage: <https://nikolas-chambers.github.io/shaman-cli/>**  
**Docs: <https://nikolas-chambers.github.io/shaman-cli/docs.html>**

A coding agent for your terminal, written in C++. Heavily inspired by
[opencode](https://github.com/anomalyco/opencode) and Claude Code, and by what the
community needs from a coding agent: one native binary, no runtime, free models out of the box.

```sh
shaman                               # full-screen UI
shaman run "why is the build failing?"
git diff | shaman run "review this"
shaman web                           # browser UI
```

## Install

Requires CMake 3.24+, a C++23 compiler (GCC 13, Clang 17, MSVC 19.38), libcurl and OpenSSL.

```sh
# Debian/Ubuntu: apt install cmake ninja-build libcurl4-openssl-dev libssl-dev nlohmann-json3-dev
# macOS:         brew install cmake ninja curl openssl nlohmann-json
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build && sudo cmake --install build
```

Add `-DSHAMAN_BUILD_DESKTOP=ON` for the desktop app (`shaman-desktop`; WebView2 on Windows, WebKitGTK on Linux).

## Models

Works immediately with the free tier of [OpenCode Zen](https://opencode.ai/zen) (Big Pickle and friends, no account).
If a free model is rate limited, shaman moves to the next one.

Add a key for anything else: `shaman auth login anthropic`, or set `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`,
`GEMINI_API_KEY`, `OPENROUTER_API_KEY`, `XAI_API_KEY`, `DEEPSEEK_API_KEY`, `GROQ_API_KEY`, `MISTRAL_API_KEY` or
`OPENCODE_API_KEY`. Ollama and LM Studio work locally with no key. `shaman models` lists what's available.

## Use

| | |
|---|---|
| `shaman [message]` | Full-screen UI. `Ctrl-P` commands, `Tab` switch agent, `Esc` stop |
| `shaman run [message]` | One-shot. `-f` attaches files/images, `@path` inlines files, `--format json` |
| `shaman serve` / `web` | HTTP API and web UI ([docs/SERVER.md](docs/SERVER.md)) |
| `shaman acp` | Agent Client Protocol, for Zed and other editors |
| `shaman mcp list\|auth\|serve` | MCP servers (stdio, HTTP, OAuth); `serve` exposes shaman's tools over MCP |
| `shaman github install` | `/shaman` comments on issues and PRs via GitHub Actions |
| `shaman export\|import\|share\|stats` | Sessions as Markdown, JSON or a shareable HTML page |
| `shaman upgrade` | Self-update from GitHub releases |

Agents: `build` (default), `plan` (read-only), and subagents `explore` and `general`. Add your own in
`.shaman/agents/*.md` and commands in `.shaman/commands/*.md`.

Skills: 23 built in, loaded only when relevant: code-review, debugging, testing, git-workflow, refactoring,
security-review, performance, documentation, api-design, dependency-upgrade, migrations, frontend-ui,
webapp-testing, data-analysis, research, ci-cd, docker, mcp-builder, skill-creator, and pdf / docx / xlsx / pptx
with helper scripts. Add your own in `.shaman/skills/<name>/SKILL.md` (`shaman debug skills` lists them).

Tools: `read write edit apply_patch list glob grep bash webfetch websearch lsp skill task todowrite todoread`, plus
MCP and plugin tools. After every edit shaman runs the project's formatter and reports language-server errors.

## Config

`shaman.json` in the project or `~/.config/shaman/`. Full reference: [docs/CONFIG.md](docs/CONFIG.md).

```jsonc
{
  "model": "anthropic/claude-sonnet-5",
  "permission": { "edit": "ask", "bash": { "*": "ask", "npm test*": "allow", "git push*": "deny" } },
  "mcp": { "docs": { "url": "https://example.com/mcp" } },
  "plugin": ["./tools/guard.py"]
}
```

## Debugging

```sh
shaman --debug=provider,tool run "..."   # categories: config provider http sse tool permission session agent mcp
shaman --trace run.jsonl run "..."       # every request, stream chunk, tool call and permission decision
shaman debug prompt                      # the exact system prompt; also config models agents tools lsp ...
```

## Extending

Plugins are programs in any language that hook tool calls, permissions, prompts and events, and add tools. SDKs for
[Python](sdk/python) and [TypeScript](sdk/typescript); see [docs/PLUGINS.md](docs/PLUGINS.md). The same folders have
clients for the HTTP API.

## Improvements over opencode

Native binary with millisecond startup · safer defaults (edits and shell ask, read-before-edit, a loop guard) ·
automatic free-model fallback · crash-safe append-only sessions · undo that never touches your git state ·
formatters only where the project configured them · plugins in any language · categorised debug logs and JSONL
traces · an offline test suite driving the real binary against mock model, MCP and LSP servers.

## Status

Tested on Linux: 27 unit tests, 62 offline end-to-end checks, and a live suite (`tests/e2e/live.sh`) passing on Gemini 3.1 and 3.5 Flash-Lite, plus the web UI in Chromium, the TUI in xterm and the desktop app under X11. macOS and Windows builds are set up in CI but not yet
verified; the Windows desktop app and MCP stdio servers on Windows are untested.

## Development

```sh
ctest --test-dir build --output-on-failure      # unit + end-to-end (mock servers in tests/mock/)
```

Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). License: MIT.

---

Inspired by [opencode](https://github.com/anomalyco/opencode) (MIT). Free models by [OpenCode Zen](https://opencode.ai/zen).
Not affiliated with opencode.

---

### 💼 For hire

AI, learning, app, and web development. Frontend, back end, and servers. For
Windows, Linux, and Android. In Java, C, C++, Python, Go, asm, and more — UI,
GUI and command line. I like working with cutting-edge technology and turning
it into something useful for the community, not just another demo. Need an
agent, a CLI, a developer tool, or just about anything else? I can probably put
the pieces together — [email me](mailto:nikolasjonchambers@gmail.com) —
I'm open to freelance work.

---

<table>
<tr><td>

### ☕ Buy me a coffee?

**Venmo · Cash App · PayPal — "NikAndRigatoni" (Nikolas Chambers)**

The honest version: my dog and I are living in the car right now. I spend my
days writing code anyway - bringing old projects of mine back to life one at a
time, and learning everything I can along the way. If anything here was useful
to you, a few bucks goes to dog food, gas, and keeping the laptop running, and
it buys me more hours to keep building. Either way, thanks for reading this far.

</td></tr>
</table>
