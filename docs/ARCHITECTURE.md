# Architecture

Everything lives in `src/shaman/`, one directory per module. Dependencies point downward only.

```
cli/         entry point, subcommands, line-based REPL, terminal/JSON rendering, `shaman debug`
tui/         full-screen UI: raw terminal, markdown rendering, themes, pickers, dialogs
server/      HTTP API + embedded web UI (web/index.html)
acp/         Agent Client Protocol for editors
session/     the agent loop (runner), store, system prompt, snapshots, input, formatters, archive, secret redaction
agent/       agent definitions: built-ins, config, markdown files; prompts
command/     custom slash commands          skill/   SKILL.md discovery
tool/        Tool interface, registry, built-in tools (tool/builtin/)
lsp/         language-server client and manager
mcp/         MCP client (stdio, streamable HTTP, OAuth) and MCP serve mode
plugin/      out-of-process plugin host
permission/  layered allow/ask/deny rules and the interactive gate
provider/    Provider interface, OpenAI chat, OpenAI Responses and Anthropic protocols, catalog, registry
auth/        saved API keys                 github/  Actions integration     update/  self-update
extras/      notifications, git worktrees, cron scheduling, clipboard
hooks/       user shell hooks around tools, prompts and stopping
index/       codebase symbol index (per-language patterns, cached, incremental) behind the symbols tool
sandbox/     bubblewrap / sandbox-exec wrapping for bash
diff/        Myers unified diffs for edit tools
llm/         provider-neutral messages, requests and stream events
http/        libcurl transport and SSE parser
config/      layered JSONC config with {env:} / {file:} substitution
core/        Result/Error, logging and tracing, paths (XDG or portable), strings, processes and sockets (per-OS)
apps/        main.cpp (shaman), desktop.cpp (shaman-desktop)
```

## A turn, end to end

1. `cli` builds an `App` (config, providers, agents, tools, MCP, session store) and a `Runner`.
2. `Runner::prompt` snapshots the worktree, appends the user message to the session, and builds a
   `ChatRequest`: the system prompt (`session/system_prompt`), the history and the agent's enabled tools.
3. The resolved `Provider` streams `StreamEvent`s (text, reasoning, whole tool calls, usage, finish).
   Retryable failures back off, then walk the free-model fallback chain.
4. Each tool call goes through the doom-loop guard, then `Tool::run`. Tools ask the `permission::Gate`
   themselves, because the subject (command, path, URL) depends on their input.
5. Tool results are appended as a user message and the loop continues until the model stops calling tools,
   the agent's `max_steps` is reached, or the user cancels. Near the context limit the history is compacted.

Every step emits `session::Events` (rendered by `cli/render`) plus debug logs and trace records.

Around that loop: hooks run before the user message is stored (UserPromptSubmit, SessionStart), around each tool
(PreToolUse, PostToolUse) and when the model stops (Stop, which can send it back to work). The permission mode lives
in the runner and every agent's gate follows it, so the UI can change it mid-turn; `plan_exit` hands the rest of a turn
from the plan agent to build. Per-request settings resolve from the agent, the model entry and `/effort`: temperature,
reasoning effort, extra body fields, the model's tool filter and its context budget (prune and compaction points).
Reasoning that must go back to the API (Anthropic signatures, Responses encrypted items) is stored on
`ReasoningPart` and replayed by that provider only.

Each top-level turn records where its user message starts (`Info::turn_starts`) next to its worktree snapshot
(`Info::snapshots`). `Runner::revert(n)` restores snapshot `n` and truncates the log there; `Runner::fork(n)` copies
the log up to it into a new session. Edit tools attach a unified diff to their result; it is stored and shown in
every UI but never sent to the model.

## Extension points

| To add | Implement | Register in |
|---|---|---|
| A tool | `tool::Tool` | `tool::register_builtins` (`tool/builtin/system.cpp`) |
| A wire protocol (Responses, Gemini native, Bedrock) | `provider::Provider`, add an `Api` value | `provider::make` (`provider/factory.cpp`) |
| A key-based provider | a `ProviderInfo` entry | `provider::builtin()`; keys resolve via `ProviderInfo::env` |
| A front end | `session::Events` + an `Asker` | construct a `Runner` from `App::services` (see tui/, server/, acp/) |
| An agent | markdown file or config entry | `agent::Registry` loads them automatically |
| A platform | `core/process_<os>.cpp`, `core/paths.cpp` | CMake picks by platform |
| Behaviour without recompiling | a plugin ([PLUGINS.md](PLUGINS.md)) or `plugin::Hooks` in C++ | config `plugin` |

## Conventions

- Errors are values: `Result<T>` (`std::expected<T, Error>`). No exceptions cross module boundaries.
- Pure logic is separated from I/O so it can be unit tested: `OpenAIChatDecoder`, `SseParser`, `apply_edit`,
  `permission::Rules`.
- Files on disk are written atomically (temp + rename) or append-only.
- Logging goes through `log::debug(Cat, ...)` so every subsystem can be switched on alone.
