"""shaman plugin SDK for Python (no dependencies).

A plugin is a process that talks newline-delimited JSON-RPC 2.0 over stdio.
This module handles the protocol; you register hooks and tools:

    from shaman_plugin import Plugin

    plugin = Plugin("guard")

    @plugin.hook("tool.before")
    def no_force_push(ev):
        if ev["tool"] == "bash" and "push --force" in ev["input"].get("command", ""):
            return {"block": "force pushes are not allowed"}

    @plugin.tool("word_count", "Count words in a file",
                 {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]})
    def word_count(input, ctx):
        return str(len(open(input["path"]).read().split()))

    plugin.run()

Hooks and what they may return:
    tool.before     {session, tool, input}                -> {"input": {...}} | {"block": "reason"} | None
    tool.after      {session, tool, input, output, is_error} -> {"output": "..."} | None
    permission.ask  {permission, subject, title}          -> {"action": "allow" | "deny"} | None
    chat.system     {agent, system}                       -> {"system": "..."} | None
    chat.message    {session, text}                       -> {"text": "..."} | None
    event           {type, data}   (notification: prompt, tool.end, idle, error)

Tools return a string, or a dict {"output", "is_error", "title"}.
Register a plugin in shaman.json ("plugin": ["./my_plugin.py"]) or drop an
executable file into .shaman/plugins/.
"""
import json
import sys
import traceback

__all__ = ["Plugin"]


class Plugin:
    def __init__(self, name):
        self.name = name
        self._hooks = {}
        self._tools = {}
        self.root = None

    def hook(self, name):
        def register(fn):
            self._hooks[name] = fn
            return fn
        return register

    def on_event(self, fn):
        self._hooks["event"] = fn
        return fn

    def tool(self, name, description, parameters=None, permission=None):
        def register(fn):
            spec = {"name": name, "description": description,
                    "parameters": parameters or {"type": "object", "properties": {}}}
            if permission:
                spec["permission"] = permission
            self._tools[name] = (spec, fn)
            return fn
        return register

    def log(self, *args):
        """Debug output goes to stderr; stdout is the protocol channel."""
        print(*args, file=sys.stderr, flush=True)

    def _reply(self, msg_id, result=None, error=None):
        out = {"jsonrpc": "2.0", "id": msg_id}
        if error is not None:
            out["error"] = {"code": -32000, "message": error}
        else:
            out["result"] = result if result is not None else {}
        sys.stdout.write(json.dumps(out) + "\n")
        sys.stdout.flush()

    def _dispatch(self, method, params):
        if method == "initialize":
            self.root = params.get("root")
            return {"name": self.name, "hooks": list(self._hooks), "tools": [s for s, _ in self._tools.values()]}
        if method == "tool.call":
            spec, fn = self._tools[params["name"]]
            result = fn(params.get("input", {}), params)
            if isinstance(result, dict):
                return result
            return {"output": "" if result is None else str(result)}
        fn = self._hooks.get(method)
        return (fn(params) if fn else None) or {}

    def run(self):
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            method, params = msg.get("method"), msg.get("params") or {}
            if "id" not in msg:  # notification
                fn = self._hooks.get("event") if method == "event" else None
                if fn:
                    try:
                        fn(params)
                    except Exception:
                        traceback.print_exc(file=sys.stderr)
                continue
            try:
                self._reply(msg["id"], self._dispatch(method, params))
            except Exception as e:
                traceback.print_exc(file=sys.stderr)
                self._reply(msg["id"], error=str(e))
