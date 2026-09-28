"""Client for the shaman HTTP API (`shaman serve`). No dependencies.

    from shaman_client import Shaman

    sh = Shaman("http://127.0.0.1:4096")          # token="..." if started with --token
    session = sh.create_session()
    for event in sh.prompt(session["id"], "explain src/main.cpp"):
        if event["type"] == "text":
            print(event["text"], end="", flush=True)
        elif event["type"] == "permission":
            sh.reply_permission(event["id"], "once")   # or "always" / "reject"

Events: text, reasoning, tool_start, tool_end, step, notice, permission, done, error.
"""
import json
import urllib.request

__all__ = ["Shaman"]


class Shaman:
    def __init__(self, url="http://127.0.0.1:4096", token=None):
        self.url = url.rstrip("/")
        self.token = token

    def _request(self, method, path, body=None, stream=False):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.url + path, data=data, method=method)
        req.add_header("Content-Type", "application/json")
        if self.token:
            req.add_header("Authorization", "Bearer " + self.token)
        res = urllib.request.urlopen(req, timeout=None if stream else 60)
        return res if stream else json.loads(res.read() or b"null")

    def health(self): return self._request("GET", "/health")
    def models(self): return self._request("GET", "/models")
    def agents(self): return self._request("GET", "/agents")
    def sessions(self): return self._request("GET", "/session")
    def session(self, sid): return self._request("GET", f"/session/{sid}")
    def messages(self, sid): return self._request("GET", f"/session/{sid}/message")
    def delete_session(self, sid): return self._request("DELETE", f"/session/{sid}")
    def abort(self, sid): return self._request("POST", f"/session/{sid}/abort", {})
    def undo(self, sid): return self._request("POST", f"/session/{sid}/undo", {})
    def compact(self, sid): return self._request("POST", f"/session/{sid}/compact", {})
    def reply_permission(self, pid, reply): return self._request("POST", f"/permission/{pid}", {"reply": reply})

    def create_session(self, agent=None, model=None):
        body = {}
        if agent: body["agent"] = agent
        if model: body["model"] = model
        return self._request("POST", "/session", body)

    def prompt(self, sid, text, model=None, agent=None, files=None):
        """Send a message and yield events as they stream in."""
        body = {"text": text}
        if model: body["model"] = model
        if agent: body["agent"] = agent
        if files: body["files"] = files
        res = self._request("POST", f"/session/{sid}/message", body, stream=True)
        event, data = "message", []
        for raw in res:
            line = raw.decode("utf-8").rstrip("\r\n")
            if not line:
                if data:
                    payload = json.loads("\n".join(data))
                    payload["type"] = event
                    yield payload
                event, data = "message", []
            elif line.startswith("event:"):
                event = line[6:].strip()
            elif line.startswith("data:"):
                data.append(line[5:].lstrip(" "))
