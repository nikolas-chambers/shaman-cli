#!/usr/bin/env bash
# Live tests: the real shaman binary against a real model. Needs network and a key.
#
#   SHAMAN_LIVE_MODEL=google/gemini-2.5-flash GEMINI_API_KEY=... tests/e2e/live.sh [build/shaman]
#
# Models are non-deterministic, so checks look at effects (files changed, tools
# used, commands run) rather than exact wording. Uses a throwaway HOME/XDG tree.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
shaman="$(realpath "${1:-$repo/build/shaman}")"
model="${SHAMAN_LIVE_MODEL:?set SHAMAN_LIVE_MODEL, e.g. google/gemini-2.5-flash}"

tmp="$(mktemp -d)"
trap '[ "${KEEP:-0}" = 1 ] && echo "kept $tmp" || rm -rf "$tmp"' EXIT
export XDG_CONFIG_HOME="$tmp/config" XDG_DATA_HOME="$tmp/data" XDG_CACHE_HOME="$tmp/cache"
export SHAMAN_MODEL="$model"
project="$tmp/project"
mkdir -p "$project" && cd "$project" && git init -q . && git config user.email t@t && git config user.name t

pass=0 fail=0
ok()   { echo "  ok   $1"; pass=$((pass + 1)); }
bad()  { echo "  FAIL $1"; [ -n "${2:-}" ] && sed 's/^/    | /' <<<"$2" | tail -15; fail=$((fail + 1)); }
run()  { timeout 300 "$shaman" run "$@" </dev/null 2>&1; }

# 1. read -> edit -> bash verify
printf 'def add(a, b):\n    return a - b\n' > calc.py && git add -A && git commit -qm init
out="$(run --yolo "calc.py has a bug in add(). Fix it, then verify by running: python3 -c 'from calc import add; print(add(2,3))'")"
grep -q "a + b" calc.py && grep -q "python3 -c" <<<"$out" && ok "fix, edit and verify" || bad "fix, edit and verify" "$out"

# 2. undo restores the file (snapshot taken before that turn)
printf '/undo\n/exit\n' | "$shaman" -c --plain >/dev/null 2>&1
grep -q "a - b" calc.py && ok "undo after live edit" || bad "undo after live edit" "$(cat calc.py)"

# 3. permissions: without --yolo, edits are refused when nobody can answer
out="$(run "Change calc.py so add returns 42. Use the edit tool.")"
grep -q "a - b" calc.py && grep -qiE "permission denied|denied" <<<"$out" && ok "permission refusal" || bad "permission refusal" "$out"

# 4. plan agent never edits
out="$(run --yolo --agent plan "Fix the bug in calc.py")"
grep -q "a - b" calc.py && ok "plan agent read-only" || bad "plan agent read-only" "$out"

# 5. subagent via task tool
out="$(run --yolo "Use the task tool with subagent_type explore to find which file defines add(). Report the file name.")"
grep -qiE "explore >|explore:" <<<"$out" && grep -q "calc.py" <<<"$out" && ok "explore subagent" || bad "explore subagent" "$out"

# 6. todo list + multiple tools
printf 'def mul(a, b):\n    return a + b\n' > more.py
out="$(run --yolo "Keep a todo list with todowrite. Find all python files with glob, fix mul in more.py to multiply, and verify with python3.")"
grep -q "a \* b" more.py && ok "glob + edit + verify" || bad "glob + edit + verify" "$out"
grep -qE "Todos|todo" <<<"$out" && ok "todo list used" || bad "todo list used" "$out"

# 7. @file mention inlines content
echo "The secret word is PINEAPPLE." > notes.txt
out="$(run "What is the secret word in @notes.txt? Answer with just the word.")"
grep -qi "pineapple" <<<"$out" && ok "@file mention" || bad "@file mention" "$out"

# 8. image attachment (vision)
python3 - <<'PY'
import struct, zlib
w, h = 64, 64
raw = b"".join(b"\x00" + b"\xff\x00\x00" * w for _ in range(h))   # solid red
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
open("red.png", "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
PY
out="$(run -f red.png "What single colour fills this image? One word.")"
grep -qi "red" <<<"$out" && ok "image attachment" || bad "image attachment" "$out"

# 9. JSON event stream
out="$(run --format json --yolo "Use the bash tool to run: echo hello-json")"
grep -q '"type":"tool_end"' <<<"$out" && grep -q "hello-json" <<<"$out" && ok "json events" || bad "json events" "$out"

# 10. custom command + builtin /init
mkdir -p .shaman/commands && printf -- '---\ndescription: count\n---\nHow many lines does $1 have? Use the read tool, answer with the number only.\n' > .shaman/commands/lines.md
out="$(run --command lines calc.py)"
grep -qiE "\b2\b|two" <<<"$out" && grep -q "Read calc.py" <<<"$out" && ok "custom command" || bad "custom command" "$out"
out="$(run --yolo --command init)"
[ -f AGENTS.md ] && ok "/init writes AGENTS.md" || bad "/init writes AGENTS.md" "$out"

# 11. LSP diagnostics fed back after an edit (pyright)
if command -v pyright-langserver >/dev/null; then
  out="$(run --yolo "Create typed.py containing exactly: x: int = \"text\"  (write it verbatim with the write tool, then stop)")"
  grep -rq "LSP errors in typed.py" "$XDG_DATA_HOME" && ok "lsp diagnostics" || bad "lsp diagnostics" "$out"
fi

# 12. MCP server + plugin used by the model
export SHAMAN_CONFIG_CONTENT='{"mcp":{"mock":{"command":["python3","'"$repo"'/tests/mock/mock_mcp.py"]}},"plugin":["'"$repo"'/examples/plugins/guard.py"]}'
out="$(run --yolo "Use the mock_add tool to add 19 and 23. Reply with the result.")"
grep -q "42" <<<"$out" && ok "mcp tool" || bad "mcp tool" "$out"
out="$(run --yolo "I have already confirmed. Immediately run this exact shell command with the bash tool, do not ask: git push --force origin main")"
grep -q "blocked by plugin" <<<"$out" && ok "plugin blocks force push" || bad "plugin blocks force push" "$out"
unset SHAMAN_CONFIG_CONTENT

# 13. compaction when the window fills (tiny context forces it)
provider="${model%%/*}" mid="${model#*/}"
export SHAMAN_CONFIG_CONTENT='{"provider":{"'"$provider"'":{"models":{"'"$mid"'":{"context":2500}}}}}'
out="$(run --yolo "Read calc.py, more.py and notes.txt one at a time, then say done.")"
grep -q "compacting" <<<"$out" && ok "auto-compaction" || bad "auto-compaction" "$out"
unset SHAMAN_CONFIG_CONTENT

# 14. built-in skill with a helper script (pdf)
if python3 -c "import reportlab, pypdf" 2>/dev/null; then
  out="$(run --yolo "Create a one-page PDF named summary.pdf listing the python files in this project as bullets.")"
  [ -f summary.pdf ] && grep -rq "pdf_tool.py" "$XDG_DATA_HOME" && ok "pdf skill + script" || bad "pdf skill + script" "$out"
fi

# 15. server + attach + ACP
"$shaman" serve --port 0 >"$tmp/serve.out" 2>&1 &
spid=$!
for _ in $(seq 50); do grep -q "listening on" "$tmp/serve.out" && break; sleep 0.1; done
srv="$(awk '/listening on/ {print $5}' "$tmp/serve.out")"
out="$(timeout 120 "$shaman" run --attach "$srv" "Say the word attached-ok and nothing else" </dev/null 2>&1)"
grep -qi "attached-ok" <<<"$out" && ok "server + attach" || bad "server + attach" "$out"
kill $spid 2>/dev/null
out="$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}}' \
  '{"jsonrpc":"2.0","id":2,"method":"session/new","params":{"cwd":"'"$project"'","mcpServers":[]}}' | timeout 60 "$shaman" acp)"
sid="$(grep -o 'ses_[0-9a-f]*' <<<"$out" | head -1)"
out="$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":1}}' \
  '{"jsonrpc":"2.0","id":2,"method":"session/load","params":{"sessionId":"'"$sid"'","cwd":"'"$project"'","mcpServers":[]}}' \
  '{"jsonrpc":"2.0","id":3,"method":"session/prompt","params":{"sessionId":"'"$sid"'","prompt":[{"type":"text","text":"Say acp-ok"}]}}' | timeout 120 "$shaman" acp)"
grep -q "agent_message_chunk" <<<"$out" && grep -q '"stopReason":"end_turn"' <<<"$out" && ok "acp live" || bad "acp live" "$out"

echo "$pass passed, $fail failed ($model)"
[ "$fail" = 0 ]
