#!/usr/bin/env python3
"""Fake language server (LSP over stdio, Content-Length framing).

Any line containing ERROR produces an error diagnostic at that line; hover
answers "mock hover"; definition points at line 1; documentSymbol lists
lines that start with "def ". Configure it for ".mock" files:
  "lsp": {"servers": {"mock": {"command": ["python3", "tests/mock/mock_lsp.py"], "extensions": [".mock"]}}}
"""
import json
import sys

docs = {}


def read():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        line = line.decode().strip()
        if not line:
            break
        k, v = line.split(":", 1)
        headers[k.lower()] = v.strip()
    return json.loads(sys.stdin.buffer.read(int(headers["content-length"])))


def send(msg):
    body = json.dumps(msg).encode()
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
    sys.stdout.buffer.flush()


def publish(uri):
    diags = [{"range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": 5}},
              "severity": 1, "message": "mock error: found ERROR", "source": "mock"}
             for i, line in enumerate(docs.get(uri, "").split("\n")) if "ERROR" in line]
    send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": diags}})


while True:
    msg = read()
    if msg is None:
        break
    method, params = msg.get("method"), msg.get("params") or {}
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": {"capabilities": {"textDocumentSync": 1, "hoverProvider": True}}})
        # Exercise the client's handling of server->client requests.
        send({"jsonrpc": "2.0", "id": 900, "method": "workspace/configuration", "params": {"items": [{"section": "mock"}]}})
    elif method == "textDocument/didOpen":
        docs[params["textDocument"]["uri"]] = params["textDocument"]["text"]
        publish(params["textDocument"]["uri"])
    elif method == "textDocument/didChange":
        docs[params["textDocument"]["uri"]] = params["contentChanges"][-1]["text"]
        publish(params["textDocument"]["uri"])
    elif method == "textDocument/hover":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": {"contents": {"kind": "plaintext", "value": "mock hover"}}})
    elif method == "textDocument/definition":
        uri = params["textDocument"]["uri"]
        send({"jsonrpc": "2.0", "id": msg["id"], "result": [{"uri": uri, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 1}}}]})
    elif method == "textDocument/documentSymbol":
        uri = params["textDocument"]["uri"]
        syms = [{"name": line[4:].split("(")[0], "kind": 12, "range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": 1}},
                 "selectionRange": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": 1}}}
                for i, line in enumerate(docs.get(uri, "").split("\n")) if line.startswith("def ")]
        send({"jsonrpc": "2.0", "id": msg["id"], "result": syms})
    elif method == "shutdown":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
    elif method == "exit":
        break
    elif "id" in msg and method:
        send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
