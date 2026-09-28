#!/usr/bin/env python3
"""Fake OpenCode Zen server for tests and debugging.

Speaks just enough of the OpenAI chat-completions streaming protocol for
shaman: GET /models and POST /chat/completions (SSE). Point shaman at it with
SHAMAN_ZEN_URL=http://127.0.0.1:<port>.

Behaviour is driven by the latest user message, so one server covers every
scenario:

  "read <file>"     calls the read tool on <file>, then quotes the first line
  "run <command>"   calls the bash tool, then reports the exit code
  "loop"            calls the same tool forever (doom-loop guard test)
  anything else     replies "echo: <message>"

Flags:
  --port N              listen port (default 8765; 0 picks a free one)
  --rate-limit MODEL    answer 429 for MODEL (repeatable) to test fallback
  --log FILE            append every request body as JSONL

It prints "listening on <port>" once ready.
"""
import argparse
import json
import http.server
import sys

FREE_MODELS = ["big-pickle", "space-bunny-free", "nemotron-3-ultra-free", "longcat-2.5-preview-free"]


def last_user_text(messages):
    for m in reversed(messages):
        if m["role"] == "user":
            return m["content"] if isinstance(m["content"], str) else ""
    return ""


def make_handler(args):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send_json(self, status, obj):
            data = json.dumps(obj).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path.rstrip("/").endswith("/models"):
                self.send_json(200, {"object": "list", "data": [{"id": m} for m in FREE_MODELS + ["paid-model"]]})
            else:
                self.send_json(404, {"error": {"message": "not found"}})

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            if args.log:
                with open(args.log, "a") as f:
                    f.write(json.dumps(body) + "\n")
            if not self.path.rstrip("/").endswith("/chat/completions"):
                return self.send_json(404, {"error": {"message": "not found"}})
            if body.get("model") in args.rate_limit:
                return self.send_json(429, {"error": {"message": "rate limited"}})

            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            model = body.get("model", "")

            def event(delta=None, finish=None, usage=None):
                chunk = {"model": model, "choices": [{"index": 0, "delta": delta or {}}]}
                if finish:
                    chunk["choices"][0]["finish_reason"] = finish
                if usage:
                    chunk["usage"] = usage
                self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
                self.wfile.flush()

            def tool_call(name, arguments):
                event({"tool_calls": [{"index": 0, "id": "call_1", "type": "function",
                                       "function": {"name": name, "arguments": ""}}]})
                raw = json.dumps(arguments)
                half = len(raw) // 2  # split arguments across chunks like real servers do
                event({"tool_calls": [{"index": 0, "function": {"arguments": raw[:half]}}]})
                event({"tool_calls": [{"index": 0, "function": {"arguments": raw[half:]}}]})
                event(finish="tool_calls")

            messages = body.get("messages", [])
            last = messages[-1] if messages else {"role": "user", "content": ""}
            prompt = last_user_text(messages)
            usage = {"prompt_tokens": 100, "completion_tokens": 10}

            if last["role"] == "tool":
                if prompt.startswith("loop"):
                    tool_call("glob", {"pattern": "*.nothing"})
                else:
                    content = last.get("content", "")
                    first = content.split("\n")[0].split("\t", 1)[-1].strip()
                    event({"content": f"tool said: {first}"})
                    event(finish="stop", usage=usage)
            elif prompt.startswith("read "):
                event({"content": "Reading. "})
                tool_call("read", {"filePath": prompt[5:].strip()})
            elif prompt.startswith("run "):
                tool_call("bash", {"command": prompt[4:].strip(), "description": "test command"})
            elif prompt.startswith("loop"):
                tool_call("glob", {"pattern": "*.nothing"})
            else:
                for word in f"echo: {prompt}".split(" "):
                    event({"content": word + " "})
                event(finish="stop", usage=usage)
            self.wfile.write(b"data: [DONE]\n\n")

    return Handler


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--rate-limit", action="append", default=[])
    p.add_argument("--log")
    args = p.parse_args()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args))
    print(f"listening on {server.server_address[1]}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)


if __name__ == "__main__":
    main()
