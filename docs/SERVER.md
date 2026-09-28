# HTTP API

`shaman serve [--port 4096] [--host 127.0.0.1] [--token T] [--yolo]` serves the web UI at `/` and this API.
With `--token`, API calls need `Authorization: Bearer T`. `shaman web` does the same on a free port and opens a
browser; `shaman-desktop` embeds it in a native window.

| Method | Path | |
|---|---|---|
| GET | `/health` | `{"version"}` |
| GET | `/info` | `{"version", "project", "default_model"}` |
| GET | `/models`, `/agents`, `/commands`, `/config` | models include `context`, `free`, `vision` |
| GET / POST | `/session` | list / create (`{"agent"?, "model"?}`) |
| GET / DELETE | `/session/:id` | |
| GET | `/session/:id/message` | full message log |
| POST | `/session/:id/message` | `{"text", "model"?, "agent"?, "files"?, "goal"?}` → SSE stream; `{"command", "arguments"}` runs a custom command |
| POST | `/session/:id/abort`, `/undo`, `/compact` | |
| POST | `/session/:id/title` | `{"title"}` rename |
| GET | `/session/:id/turns` | user messages you can go back to: `[{"turn", "text"}]` |
| POST | `/session/:id/revert` | `{"turn"}` drop that message and everything after it, restore files; returns its `text` |
| POST | `/session/:id/fork` | `{"turn"?}` copy the session up to that message (all of it by default) into a new one |
| GET | `/session/:id/share` | self-contained HTML page of the conversation |
| POST | `/upload?name=f.png` | raw body; saved under the data dir, returns `{"path", "name", "bytes"}` to pass in `files` |
| POST | `/permission/:id` | `{"reply": "once" \| "always" \| "reject"}` |
| POST | `/question/:id` | `{"answer": "..."}` (empty = skip) |
| GET | `/event` | SSE stream of every session's events |

Stream events (each `data:` is JSON with a `session` field): `text`, `reasoning`, `tool_start`, `tool_end` (with a
unified `diff` for file edits), `step`,
`notice`, `permission` (answer via `/permission/:id`), `question` (`{id, question, options, multiple}`, answer via
`/question/:id`), `done`, `error`.

```sh
id=$(curl -s -X POST localhost:4096/session -d '{}' | jq -r .id)
curl -N -X POST localhost:4096/session/$id/message -d '{"text":"list the TODOs"}'
shaman run --attach http://localhost:4096 "same thing from another terminal"
```

Clients: [sdk/python/shaman_client.py](../sdk/python/shaman_client.py), [sdk/typescript/client.ts](../sdk/typescript/client.ts).
