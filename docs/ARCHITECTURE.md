# Architecture

Everything lives in `src/shaman/`, one directory per module. Dependencies point downward only.

```
cli/         entry point, argument parsing, REPL, terminal/JSON rendering, `shaman debug`
session/     the agent loop (runner), session store, system prompt, snapshots
agent/       agent definitions: built-ins, config, markdown files; prompts
tool/        Tool interface, registry, built-in tools (tool/builtin/)
mcp/         MCP stdio client; wraps server tools as Tools
permission/  layered allow/ask/deny rules and the interactive gate
provider/    Provider interface, wire protocols, model catalog, registry
llm/         provider-neutral messages, requests and stream events
http/        libcurl transport and SSE parser
config/      layered JSONC config with {env:} / {file:} substitution
core/        Result/Error, logging and tracing, paths, strings, processes (per-OS)
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

## Extension points

| To add | Implement | Register in |
|---|---|---|
| A tool | `tool::Tool` | `tool::register_builtins` (`tool/builtin/system.cpp`) |
| A wire protocol (Anthropic, Responses, Gemini) | `provider::Provider`, add an `Api` value | `provider::make` |
| A key-based provider | a `ProviderInfo` entry | `provider::builtin()`; keys resolve via `ProviderInfo::env` |
| A front end (TUI, HTTP server) | `session::Events` + an `Asker` | construct a `Runner` from `App::services` |
| An agent | markdown file or config entry | `agent::Registry` loads them automatically |
| A platform | `core/process_<os>.cpp`, `core/paths.cpp` | CMake picks by platform |
| A plugin SDK | hook points around `Runner::run_tool`, `permission::Gate::check` and `Events` | planned |

## Conventions

- Errors are values: `Result<T>` (`std::expected<T, Error>`). No exceptions cross module boundaries.
- Pure logic is separated from I/O so it can be unit tested: `OpenAIChatDecoder`, `SseParser`, `apply_edit`,
  `permission::Rules`.
- Files on disk are written atomically (temp + rename) or append-only.
- Logging goes through `log::debug(Cat, ...)` so every subsystem can be switched on alone.
