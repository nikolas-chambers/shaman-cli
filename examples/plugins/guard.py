#!/usr/bin/env python3
"""Example plugin: block dangerous shell commands, keep secrets out of tool
output, auto-approve test runs, and log every finished turn."""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sdk", "python"))
from shaman_plugin import Plugin  # noqa: E402

plugin = Plugin("guard")
SECRET = re.compile(r"(sk-[A-Za-z0-9_-]{20,}|AKIA[0-9A-Z]{16}|ghp_[A-Za-z0-9]{36})")


@plugin.hook("tool.before")
def block_dangerous(ev):
    cmd = ev["input"].get("command", "") if ev["tool"] == "bash" else ""
    if re.search(r"git\s+push\s+.*--force|mkfs|dd\s+if=", cmd):
        return {"block": "guard plugin: destructive command"}


@plugin.hook("tool.after")
def redact(ev):
    if SECRET.search(ev["output"]):
        return {"output": SECRET.sub("[REDACTED]", ev["output"])}


@plugin.hook("permission.ask")
def allow_tests(req):
    if req["permission"] == "bash" and re.match(r"(npm|pnpm|yarn) test|pytest|ctest|cargo test|go test", req["subject"]):
        return {"action": "allow"}


@plugin.on_event
def log(ev):
    if ev["type"] == "idle":
        plugin.log(f"guard: turn finished ({ev['data'].get('output_tokens', 0)} output tokens)")


@plugin.tool("guard_status", "Report what the guard plugin enforces")
def status(_input, _ctx):
    return "Blocks force-push/mkfs/dd, redacts API keys in tool output, auto-approves test commands."


plugin.run()
