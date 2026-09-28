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

start_mock limited --rate-limit big-pickle
export SHAMAN_ZEN_URL="$url"
check "free fallback"        "switching to opencode/space-bunny-free" "$shaman" run hi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
