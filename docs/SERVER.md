# HTTP API

`shaman serve [--port 4096] [--host 127.0.0.1] [--token T] [--yolo]` serves the web UI at `/` and this API.
With `--token`, API calls need `Authorization: Bearer T`. `shaman web` does the same on a free port and opens a
browser; `shaman-desktop` embeds it in a native window.

| Method | Path | |
|---|---|---|
| GET | `/health` | `{"version"}` |
| GET | `/models`, `/agents`, `/commands`, `/config` | |
| GET / POST | `/session` | list / create (`{"agent"?, "model"?}`) |
| GET / DELETE | `/session/:id` | |
| GET | `/session/:id/message` | full message log |
| POST | `/session/:id/message` | `{"text", "model"?, "agent"?, "files"?}` → SSE stream |
| POST | `/session/:id/abort`, `/undo`, `/compact` | |
| POST | `/permission/:id` | `{"reply": "once" \| "always" \| "reject"}` |
| GET | `/event` | SSE stream of every session's events |

Stream events (each `data:` is JSON with a `session` field): `text`, `reasoning`, `tool_start`, `tool_end`, `step`,
`notice`, `permission` (answer via `/permission/:id`), `done`, `error`.

```sh
id=$(curl -s -X POST localhost:4096/session -d '{}' | jq -r .id)
curl -N -X POST localhost:4096/session/$id/message -d '{"text":"list the TODOs"}'
shaman run --attach http://localhost:4096 "same thing from another terminal"
```

Clients: [sdk/python/shaman_client.py](../sdk/python/shaman_client.py), [sdk/typescript/client.ts](../sdk/typescript/client.ts).
