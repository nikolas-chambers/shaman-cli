#!/usr/bin/env python3
"""Minimal MCP server over stdio with no dependencies. Add tools to TOOLS."""
import json
import sys


def greet(args):
    return f"Hello, {args.get('name', 'world')}!"


TOOLS = {
    "greet": {
        "description": "Greet someone by name. Use to test the server.",
        "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}, "required": ["name"]},
        "handler": greet,
    },
}


def handle(msg):
    method, params = msg.get("method"), msg.get("params") or {}
    if method == "initialize":
        return {"protocolVersion": params.get("protocolVersion", "2025-06-18"), "capabilities": {"tools": {}},
                "serverInfo": {"name": "my-server", "version": "0.1.0"}}
    if method == "tools/list":
        return {"tools": [{"name": n, "description": t["description"], "inputSchema": t["inputSchema"]} for n, t in TOOLS.items()]}
    if method == "tools/call":
        tool = TOOLS.get(params.get("name"))
        if not tool:
            return {"content": [{"type": "text", "text": "unknown tool"}], "isError": True}
        try:
            return {"content": [{"type": "text", "text": str(tool["handler"](params.get("arguments") or {}))}]}
        except Exception as e:  # report, don't crash
            return {"content": [{"type": "text", "text": f"error: {e}"}], "isError": True}
    if method == "ping":
        return {}
    raise KeyError(method)


for line in sys.stdin:
    try:
        msg = json.loads(line)
    except ValueError:
        continue
    if "id" not in msg:
        continue  # notification
    try:
        reply = {"jsonrpc": "2.0", "id": msg["id"], "result": handle(msg)}
    except KeyError:
        reply = {"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32601, "message": "method not found"}}
    sys.stdout.write(json.dumps(reply) + "\n")
    sys.stdout.flush()
