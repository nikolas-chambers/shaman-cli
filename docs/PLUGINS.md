# Plugins

A plugin is any executable that speaks newline-delimited JSON-RPC 2.0 on stdin/stdout. Load it from config
(`"plugin": ["./guard.py"]`) or put an executable in `.shaman/plugins/` or `~/.config/shaman/plugins/`.
`stderr` is free for logging.

SDKs handle the protocol: [Python](../sdk/python/shaman_plugin.py) · [TypeScript](../sdk/typescript/plugin.ts).
Complete example: [examples/plugins/guard.py](../examples/plugins/guard.py).

## Protocol

shaman sends `initialize` `{version, protocol: 1, root}`; the plugin answers
`{name, hooks: [...], tools: [{name, description, parameters, permission?}]}`.

| Hook (request) | Params | Result (all optional) |
|---|---|---|
| `tool.before` | `session, tool, input` | `{"input": {...}}` rewrite, or `{"block": "reason"}` |
| `tool.after` | `session, tool, input, output, is_error` | `{"output": "..."}` |
| `permission.ask` | `permission, subject, title` | `{"action": "allow" \| "deny"}` (omit to defer to rules) |
| `chat.system` | `agent, system` | `{"system": "..."}` |
| `chat.message` | `session, text` | `{"text": "..."}` |
| `tool.call` | `name, input, session, root` | `{"output", "is_error"?, "title"?}` (for tools you declared) |

`event` notifications (list `"event"` in hooks): `prompt`, `tool.end`, `idle`, `error`, with a `data` object.

Hooks run in load order with a 10 s timeout; a failing plugin is logged and skipped, never fatal. Plugin tools
use the `plugin` permission (default `ask`) unless they declare their own.

C++ embedders can implement `shaman::plugin::Hooks` and add it to the `plugin::Host` directly.
