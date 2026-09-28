---
name: mcp-builder
description: Build Model Context Protocol (MCP) servers that expose an API, database or tool to agents - design tools, implement over stdio or HTTP, test with shaman. Use when asked to create an MCP server or integration.
---
# Building MCP servers

## Design first
- List the user tasks the agent should accomplish, then design **few, task-shaped tools** (e.g. `search_issues`, `create_issue`), not one tool per API endpoint.
- Tool names: `verb_noun`, snake_case. Descriptions say what it does, when to use it, and what it returns.
- Inputs: a JSON Schema with required fields, enums for fixed choices, sensible defaults, and limits (page sizes).
- Outputs: concise text the model can use directly (summaries, IDs, next steps). Paginate large results; truncate with a note.
- Errors: return `isError: true` with an actionable message ("repo not found; list repos with list_repos"), never a stack trace.
- Auth from environment variables or OAuth; never hard-code secrets.

## Implement
- Quick start with no dependencies: copy `templates/server.py` (stdio, JSON-RPC, one message per line) and add tools to the `TOOLS` table.
- Production: use the official SDKs (`pip install mcp` → `FastMCP`; `npm i @modelcontextprotocol/sdk`). Use Streamable HTTP for remote servers.
- Keep handlers small and testable; put API calls in plain functions.

## Test with shaman
```jsonc
// shaman.json
{ "mcp": { "mine": { "command": ["python3", "path/to/server.py"] } } }
```
1. `shaman mcp list` shows it connected with the right tool count.
2. `shaman run --yolo "use mine_<tool> to ..."` exercises each tool; check error paths too.
3. `shaman --debug=mcp ...` shows the raw JSON-RPC traffic when something is off.
