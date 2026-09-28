#!/usr/bin/env bash
# End-to-end tests: real shaman binary against tests/mock/mock_zen.py.
#
#   tests/e2e/run.sh [path/to/shaman]      (default: build/shaman)
#
# Everything runs in a throwaway HOME/XDG tree and project directory, so your
# real config and sessions are never touched. Set KEEP=1 to keep the temp dir.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
shaman="$(realpath "${1:-$repo/build/shaman}")"
[ -x "$shaman" ] || { echo "shaman binary not found: $shaman (build first)"; exit 2; }
command -v python3 >/dev/null || { echo "python3 is required"; exit 2; }

tmp="$(mktemp -d)"
pids=()
cleanup() {
  for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done
  [ "${KEEP:-0}" = 1 ] && echo "kept $tmp" || rm -rf "$tmp"
}
trap cleanup EXIT

export XDG_CONFIG_HOME="$tmp/config" XDG_DATA_HOME="$tmp/data" XDG_CACHE_HOME="$tmp/cache"
export NO_PROXY="127.0.0.1,localhost" no_proxy="127.0.0.1,localhost"
unset SHAMAN_MODEL SHAMAN_CONFIG SHAMAN_CONFIG_CONTENT OPENCODE_API_KEY

# start_mock <name> [mock flags...]  -> sets $url
start_mock() {
  local name="$1"; shift
  python3 "$repo/tests/mock/mock_zen.py" --port 0 --log "$tmp/$name.requests.jsonl" "$@" >"$tmp/$name.out" 2>&1 &
  pids+=($!)
  for _ in $(seq 50); do
    grep -q "listening on" "$tmp/$name.out" 2>/dev/null && break
    sleep 0.1
  done
  url="http://127.0.0.1:$(awk '/listening on/ {print $3}' "$tmp/$name.out")"
}

project="$tmp/project"
mkdir -p "$project" && cd "$project" && git init -q . 2>/dev/null
echo "the answer is 42" > notes.txt

pass=0 fail=0
check() {  # check <name> <expected substring> <command...>
  local name="$1" want="$2"; shift 2
  local out
  out="$("$@" </dev/null 2>&1)"
  if grep -qF -- "$want" <<<"$out"; then
    echo "  ok   $name"; pass=$((pass + 1))
  else
    echo "  FAIL $name"; echo "    wanted: $want"; sed 's/^/    | /' <<<"$out" | head -20; fail=$((fail + 1))
  fi
}

start_mock main
export SHAMAN_ZEN_URL="$url"
check "plain reply"          "echo: hello"                "$shaman" run hello
check "piped stdin"          "echo: from a pipe"          bash -c "echo 'from a pipe' | '$shaman' run"
check "tool round trip"      "tool said: the answer is 42" "$shaman" run read notes.txt
check "json events"          '"type":"tool_end"'          "$shaman" run --format json read notes.txt
check "bash needs approval"  "permission denied"          "$shaman" run run touch x.txt
check "yolo runs bash"       "[exit code 0]"              "$shaman" run --yolo --format json run touch y.txt
check "read-only bash ok"    "[exit code 0]"              "$shaman" run --format json run git status
check "doom loop guard"      "identical tool call"        "$shaman" run --format json loop
check "continue session"     "echo: again"                "$shaman" run -c again
check "sessions listed"      "hello"                      "$shaman" sessions
check "trace written"        "trace ok"                   bash -c "'$shaman' run --trace '$tmp/t.jsonl' hi >/dev/null && grep -q tool_call '$tmp/t.jsonl' || grep -q '\"kind\":\"request\"' '$tmp/t.jsonl' && echo 'trace ok'"
check "debug categories"     "provider"                   "$shaman" --debug=provider run hi
check "models refresh"       "space-bunny-free"           "$shaman" models --refresh
check "plan agent no edits"  '"write"'                    "$shaman" debug agents
check "undo restores file"   "undo ok"                    bash -c "
  echo original > u.txt
  '$shaman' run --yolo run 'echo changed > u.txt' >/dev/null
  printf '/undo\n/exit\n' | '$shaman' -c >/dev/null
  grep -q original u.txt && echo 'undo ok'"

# --- attachments, commands, skills --------------------------------------------
check "@file mention"        "the answer is 42"           "$shaman" run "summarise @notes.txt"
printf '\x89PNG\r\n\x1a\n0000' > pic.png
check "image attachment"     "saw image"                  "$shaman" run -f pic.png image
mkdir -p .shaman/commands .shaman/skills/demo
printf -- '---\ndescription: greet\n---\nsay $ARGUMENTS loudly\n' > .shaman/commands/hi.md
printf -- '---\nname: demo\ndescription: demo skill\n---\nDemo skill body.\n' > .shaman/skills/demo/SKILL.md
check "custom command"       "echo: say there loudly"     "$shaman" run --command hi there
check "skills discovered"    "demo skill"                 "$shaman" debug skills
check "built-in skills"      "pdf  (built-in)"            "$shaman" debug skills
check "built-in skill loads" "pdf_tool.py"                "$shaman" run 'call skill {"name":"pdf"}'
check "no .claude reading"   "0"                          bash -c "mkdir -p .claude/skills/x && printf -- '---\nname: claudeonly\ndescription: d\n---\nb\n' > .claude/skills/x/SKILL.md && '$shaman' debug skills | grep -c claudeonly"
check "skill tool"           "Demo skill body"            "$shaman" run 'call skill {"name":"demo"}'
check "apply_patch tool"     "A added.txt"                "$shaman" run --yolo 'call apply_patch {"patchText":"*** Begin Patch\n*** Add File: added.txt\n+hello patch\n*** End Patch"}'
check "websearch tool"       "https://example.com/docs"   env SHAMAN_SEARCH_URL="$url/search" "$shaman" run 'call websearch {"query":"example"}'

# --- providers and auth --------------------------------------------------------
export SHAMAN_CONFIG_CONTENT='{"provider":{"mockant":{"api":"anthropic","baseURL":"'"$url"'","apiKey":"k","models":{"claude-test":{"name":"Claude Test"}}}}}'
check "anthropic provider"   "anthropic says: the answer is 42" "$shaman" run -m mockant/claude-test read notes.txt
check "anthropic reasoning"  "hmm"                        "$shaman" run --reasoning -m mockant/claude-test hello
unset SHAMAN_CONFIG_CONTENT
check "auth login"           "Saved"                      bash -c "printf 'sk-test\n' | '$shaman' auth login openrouter"
check "auth list"            "auth"                       "$shaman" auth list
check "keyed provider shown" "OpenRouter (auth)"          "$shaman" models
check "unkeyed needs key"    "needs an API key"           "$shaman" run -m anthropic/claude-sonnet-5 hi

# --- plugins, LSP, MCP ---------------------------------------------------------
export SHAMAN_CONFIG_CONTENT='{"plugin":["'"$repo"'/examples/plugins/guard.py"],
  "lsp":{"servers":{"mock":{"command":["python3","'"$repo"'/tests/mock/mock_lsp.py"],"extensions":[".mock"]}}},
  "mcp":{"mock":{"command":["python3","'"$repo"'/tests/mock/mock_mcp.py"]}}}'
check "plugin blocks"        "blocked by plugin"          "$shaman" run --yolo 'run git push --force origin main'
check "plugin tool asks"     "permission denied"          "$shaman" run 'call guard_status {}'
check "plugin tool"          "Blocks force-push"          "$shaman" run --yolo 'call guard_status {}'
check "plugin listed"        "guard"                      "$shaman" plugins
check "lsp after write"      "LSP errors in bad.mock"     "$shaman" run --yolo 'call write {"filePath":"bad.mock","content":"ok\nERROR here\n"}'
check "lsp tool hover"       "mock hover"                 "$shaman" run 'call lsp {"operation":"hover","filePath":"bad.mock","line":1,"column":1}'
check "debug lsp"            "mock error"                 "$shaman" debug lsp bad.mock
check "mcp stdio list"       "connected, 3 tools"         "$shaman" mcp list
check "mcp stdio tool"       "echo: hi"                   "$shaman" run --yolo 'call mock_echo {"text":"hi"}'
unset SHAMAN_CONFIG_CONTENT

start_mcp() {  # start_mcp <name> [flags] -> sets $mcp_url
  local name="$1"; shift
  python3 "$repo/tests/mock/mock_mcp.py" --http 0 "$@" >"$tmp/$name.out" 2>&1 &
  pids+=($!)
  for _ in $(seq 50); do grep -q "listening on" "$tmp/$name.out" 2>/dev/null && break; sleep 0.1; done
  mcp_url="http://127.0.0.1:$(awk '/listening on/ {print $3}' "$tmp/$name.out")/mcp"
}
start_mcp sse --sse
export SHAMAN_CONFIG_CONTENT='{"mcp":{"remote":{"url":"'"$mcp_url"'"}}}'
check "mcp http+sse tool"    "echo: over http"            "$shaman" run --yolo 'call remote_echo {"text":"over http"}'
start_mcp oauth --oauth
export SHAMAN_CONFIG_CONTENT='{"mcp":{"secure":{"url":"'"$mcp_url"'"}}}'
check "mcp oauth required"   "unauthorised"               "$shaman" mcp list
check "mcp oauth login"      "Logged in to secure"        env SHAMAN_OPEN="curl -s -L -o /dev/null" "$shaman" mcp auth secure
check "mcp oauth tool"       "tool said: 5"               "$shaman" run --yolo 'call secure_add {"a":2,"b":3}'
check "mcp logout"           "Logged out of secure"       "$shaman" mcp logout secure
unset SHAMAN_CONFIG_CONTENT

check "formatter after write" "formatted with upper"      env SHAMAN_CONFIG_CONTENT='{"formatter":{"upper":{"command":["sed","-i","s/hello/HELLO/","$FILE"],"extensions":[".txt"]}}}' \
                                                          "$shaman" run --yolo 'call write {"filePath":"fmt.txt","content":"hello\n"}'

# --- sessions: export / import / share / stats ---------------------------------
check "export markdown"      "## User"                    "$shaman" export
check "export json"          "wrote"                      "$shaman" export --format json -o "$tmp/s.json"
check "import"               "imported as"                "$shaman" import "$tmp/s.json"
check "share page"           "page:"                      "$shaman" share
check "stats"                "tool calls"                 "$shaman" stats

# --- server, attach, SDK, ACP, MCP serve ---------------------------------------
"$shaman" serve --port 0 >"$tmp/serve.out" 2>&1 &
pids+=($!)
for _ in $(seq 50); do grep -q "listening on" "$tmp/serve.out" 2>/dev/null && break; sleep 0.1; done
srv="$(awk '/listening on/ {print $5}' "$tmp/serve.out")"
check "server health"        '"version"'                  curl -s --noproxy '*' "$srv/health"
check "server web ui"        "<title>shaman</title>"      curl -s --noproxy '*' "$srv/"
check "run --attach"         "echo: attached"             "$shaman" run --attach "$srv" attached
check "python client sdk"    "tool said: the answer is 42" python3 -c "
import sys; sys.path.insert(0, '$repo/sdk/python')
from shaman_client import Shaman
sh = Shaman('$srv'); s = sh.create_session()
print(''.join(e.get('text', '') for e in sh.prompt(s['id'], 'read notes.txt') if e['type'] == 'text'))"
check "acp prompt"           '"stopReason":"end_turn"'    bash -c "printf '%s\n' \
  '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":1}}' \
  '{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"session/new\",\"params\":{\"cwd\":\"$project\",\"mcpServers\":[]}}' \
  | '$shaman' acp > '$tmp/acp1.out'; sid=\$(grep -o 'ses_[0-9a-f]*' '$tmp/acp1.out' | head -1); printf '%s\n' \
  '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":1}}' \
  \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":2,\\\"method\\\":\\\"session/load\\\",\\\"params\\\":{\\\"sessionId\\\":\\\"\$sid\\\",\\\"cwd\\\":\\\"$project\\\",\\\"mcpServers\\\":[]}}\" \
  \"{\\\"jsonrpc\\\":\\\"2.0\\\",\\\"id\\\":3,\\\"method\\\":\\\"session/prompt\\\",\\\"params\\\":{\\\"sessionId\\\":\\\"\$sid\\\",\\\"prompt\\\":[{\\\"type\\\":\\\"text\\\",\\\"text\\\":\\\"hello acp\\\"}]}}\" \
  | '$shaman' acp"
check "mcp serve"            "the answer is 42"           bash -c "printf '%s\n' \
  '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}' \
  '{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}' \
  '{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"read\",\"arguments\":{\"filePath\":\"notes.txt\"}}}' \
  | '$shaman' mcp serve"

# --- GitHub, upgrade, TUI -----------------------------------------------------
echo '{"comment":{"body":"/shaman hello github","user":{"login":"nik"}},"issue":{"number":7,"title":"t","body":"b"}}' > "$tmp/event.json"
check "github dry run"       "echo: You were asked"       env GITHUB_EVENT_PATH="$tmp/event.json" GITHUB_REPOSITORY=o/r "$shaman" github run --dry-run
check "github install"       "Wrote .github/workflows/shaman.yml" "$shaman" github install
check "upgrade check"        "v9.9.9 is available"        env SHAMAN_UPDATE_API="$url" "$shaman" upgrade --check
check "tui smoke"            "tui ok"                     python3 "$repo/tests/e2e/tui_smoke.py" "$shaman"

start_mock limited --rate-limit big-pickle
export SHAMAN_ZEN_URL="$url"
check "free fallback"        "switching to opencode/space-bunny-free" "$shaman" run hi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
