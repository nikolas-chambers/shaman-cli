#!/usr/bin/env python3
"""Fake MCP server for tests and experiments.

Tools: echo {text}, add {a, b}, fail {} (returns isError).

  mock_mcp.py                       stdio transport (one JSON-RPC message per line)
  mock_mcp.py --http 0              Streamable HTTP on a free port (prints "listening on N")
  mock_mcp.py --http 0 --oauth      ...protected by OAuth 2.1: protected-resource and
                                    authorization-server metadata, dynamic client
                                    registration, PKCE (S256) and refresh tokens. The
                                    /authorize endpoint approves immediately and
                                    redirects back, so a test can "open the browser"
                                    with curl -L.
  --sse                             answer HTTP requests as text/event-stream
"""
import argparse
import base64
import hashlib
import http.server
import json
import secrets
import sys
import urllib.parse

TOOLS = [
    {"name": "echo", "description": "Echo text back", "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}},
    {"name": "add", "description": "Add two numbers", "inputSchema": {"type": "object", "properties": {"a": {"type": "number"}, "b": {"type": "number"}}}},
    {"name": "fail", "description": "Always fails", "inputSchema": {"type": "object", "properties": {}}},
]


def handle(msg):
    """Return the response for a request, or None for notifications."""
    if "id" not in msg:
        return None
    method, params = msg.get("method"), msg.get("params") or {}
    result = None
    if method == "initialize":
        result = {"protocolVersion": params.get("protocolVersion", "2025-06-18"), "capabilities": {"tools": {}},
                  "serverInfo": {"name": "mock-mcp", "version": "1.0"}}
    elif method == "tools/list":
        result = {"tools": TOOLS}
    elif method == "tools/call":
        name, args = params.get("name"), params.get("arguments") or {}
        if name == "echo":
            result = {"content": [{"type": "text", "text": "echo: " + str(args.get("text", ""))}]}
        elif name == "add":
            result = {"content": [{"type": "text", "text": str(args.get("a", 0) + args.get("b", 0))}]}
        elif name == "fail":
            result = {"content": [{"type": "text", "text": "this tool always fails"}], "isError": True}
        else:
            return {"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32602, "message": "unknown tool"}}
    elif method == "ping":
        result = {}
    else:
        return {"jsonrpc": "2.0", "id": msg["id"], "error": {"code": -32601, "message": "method not found"}}
    return {"jsonrpc": "2.0", "id": msg["id"], "result": result}


def stdio():
    for line in sys.stdin:
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        reply = handle(msg)
        if reply:
            sys.stdout.write(json.dumps(reply) + "\n")
            sys.stdout.flush()


def serve_http(port, oauth, sse):
    clients, codes, tokens = {}, {}, {}

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def base(self):
            return f"http://127.0.0.1:{self.server.server_address[1]}"

        def send(self, status, body, ctype="application/json", headers=None):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)

        def form(self):
            n = int(self.headers.get("Content-Length", 0))
            return dict(urllib.parse.parse_qsl(self.rfile.read(n).decode()))

        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            q = dict(urllib.parse.parse_qsl(url.query))
            if url.path.startswith("/.well-known/oauth-protected-resource"):
                return self.send(200, {"resource": self.base() + "/mcp", "authorization_servers": [self.base()]})
            if url.path.startswith("/.well-known/oauth-authorization-server"):
                b = self.base()
                return self.send(200, {"issuer": b, "authorization_endpoint": b + "/authorize", "token_endpoint": b + "/token",
                                       "registration_endpoint": b + "/register", "code_challenge_methods_supported": ["S256"]})
            if url.path == "/authorize":
                if q.get("client_id") not in clients or q.get("code_challenge_method") != "S256":
                    return self.send(400, {"error": "invalid_request"})
                code = secrets.token_urlsafe(16)
                codes[code] = (q["client_id"], q["code_challenge"], q["redirect_uri"])
                target = q["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": q.get("state", "")})
                self.send_response(302)
                self.send_header("Location", target)
                self.end_headers()
                return
            self.send(404, {"error": "not found"})

        def do_POST(self):
            path = urllib.parse.urlparse(self.path).path
            if path == "/register":
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
                cid = "client_" + secrets.token_hex(4)
                clients[cid] = body.get("redirect_uris", [])
                return self.send(201, {"client_id": cid, **body})
            if path == "/token":
                f = self.form()
                if f.get("grant_type") == "authorization_code":
                    entry = codes.pop(f.get("code"), None)
                    if not entry:
                        return self.send(400, {"error": "invalid_grant"})
                    cid, challenge, redirect = entry
                    digest = base64.urlsafe_b64encode(hashlib.sha256(f.get("code_verifier", "").encode()).digest()).rstrip(b"=").decode()
                    if digest != challenge or f.get("redirect_uri") != redirect or f.get("client_id") != cid:
                        return self.send(400, {"error": "invalid_grant", "error_description": "PKCE check failed"})
                elif f.get("grant_type") == "refresh_token":
                    if f.get("refresh_token") not in tokens.values():
                        return self.send(400, {"error": "invalid_grant"})
                else:
                    return self.send(400, {"error": "unsupported_grant_type"})
                access, refresh = "at_" + secrets.token_hex(8), "rt_" + secrets.token_hex(8)
                tokens[access] = refresh
                return self.send(200, {"access_token": access, "refresh_token": refresh, "token_type": "Bearer", "expires_in": 3600})
            if path == "/mcp":
                if oauth:
                    auth = self.headers.get("Authorization", "")
                    if not auth.startswith("Bearer ") or auth[7:] not in tokens:
                        return self.send(401, {"error": "unauthorized"}, headers={
                            "WWW-Authenticate": f'Bearer resource_metadata="{self.base()}/.well-known/oauth-protected-resource"'})
                msg = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
                reply = handle(msg)
                if reply is None:
                    self.send_response(202)
                    self.end_headers()
                    return
                headers = {"Mcp-Session-Id": "mock-session"}
                if sse:
                    return self.send(200, ("event: message\ndata: " + json.dumps(reply) + "\n\n").encode(), "text/event-stream", headers)
                return self.send(200, reply, headers=headers)
            self.send(404, {"error": "not found"})

    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), H)
    print(f"listening on {server.server_address[1]}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--http", type=int)
    p.add_argument("--oauth", action="store_true")
    p.add_argument("--sse", action="store_true")
    a = p.parse_args()
    if a.http is not None:
        serve_http(a.http, a.oauth, a.sse)
    else:
        stdio()
