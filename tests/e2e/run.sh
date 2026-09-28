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
export SHAMAN_HOME="$tmp/home"
export SHAMAN_NO_UPDATE_CHECK=1  # no background calls to GitHub (one check below uses the mock)  # portable root: never pick up a shaman.ini next to the dev binary
export NO_PROXY="127.0.0.1,localhost" no_proxy="127.0.0.1,localhost"
unset SHAMAN_MODEL SHAMAN_CONFIG SHAMAN_CONFIG_CONTENT OPENCODE_API_KEY
export OPENCODE_API_KEY=test-key  # the mock accepts any key

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

check "no provider set up"   "no model provider is set up" env -u GITHUB_TOKEN -u GITHUB_MODELS_TOKEN -u OPENCODE_API_KEY -u GEMINI_API_KEY -u GOOGLE_API_KEY -u ANTHROPIC_API_KEY -u OPENAI_API_KEY -u OPENROUTER_API_KEY PATH=/usr/bin:/bin "$shaman" run hi
check "dead provider fails fast" "fast"                     bash -c "s=\$(date +%s); env -u GITHUB_TOKEN -u GH_TOKEN -u GITHUB_MODELS_TOKEN PATH=/usr/bin:/bin SHAMAN_ZEN_URL=http://127.0.0.1:9 '$shaman' run hi >/dev/null 2>&1; [ \$((\$(date +%s) - s)) -lt 10 ] && echo fast || echo slow"
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

# --- new tools ----------------------------------------------------------------
printf 'alpha\nbeta\ngamma\n' > multi.txt
check "edit needs a read"     "read the file before"       "$shaman" run --yolo 'call multiedit {"filePath":"multi.txt","edits":[{"oldString":"alpha","newString":"A"}]}'
"$shaman" run --yolo read multi.txt </dev/null >/dev/null 2>&1
check "multiedit atomic"     "edit 2 failed"              "$shaman" run -c --yolo 'call multiedit {"filePath":"multi.txt","edits":[{"oldString":"alpha","newString":"A"},{"oldString":"nope","newString":"x"}]}'
check "multiedit untouched"  "alpha"                      cat multi.txt
check "read survives -c"     "Applied 2 edits"            "$shaman" run -c --yolo 'call multiedit {"filePath":"multi.txt","edits":[{"oldString":"alpha","newString":"A"},{"oldString":"gamma","newString":"G"}]}'
check "multiedit result"     "A"                          head -1 multi.txt
check "batch parallel reads" "=== 2 grep"                 "$shaman" run 'call batch {"calls":[{"tool":"read","input":{"filePath":"notes.txt"}},{"tool":"grep","input":{"pattern":"answer"}}]}'
check "batch refuses writes" "not allowed in batch"       "$shaman" run 'call batch {"calls":[{"tool":"bash","input":{"command":"ls"}}]}'
check "http tool"            "HTTP 200"                   "$shaman" run "call http {\"url\":\"$url/models\"}"
check "question without tty" "no user is available"       "$shaman" run 'call question {"question":"Which?","options":["a","b"]}'
check "memory add"           "Saved to memory"            "$shaman" run 'call memory {"action":"add","text":"Tests run with ctest"}'
check "memory in prompt"     "Tests run with ctest"       "$shaman" debug prompt
check "bash background"      "started job1 (running)"     "$shaman" run --yolo 'call bash {"command":"echo booted; sleep 30","background":true}'
python3 - <<'PY'
import json
nb = {"cells": [{"cell_type": "code", "metadata": {}, "source": ["print(1)\n"], "outputs": [{"output_type": "stream", "name": "stdout", "text": ["1\n"]}], "execution_count": 1}],
      "metadata": {}, "nbformat": 4, "nbformat_minor": 5}
json.dump(nb, open("nb.ipynb", "w"))
PY
check "bash_input to job"    "you said: hello-job"        "$shaman" run --yolo 'seq [{"tool":"bash","input":{"command":"read x; echo you said: $x; sleep 5","background":true}},{"tool":"bash_input","input":{"id":"job1","input":"hello-job"}},{"tool":"bash_output","input":{"id":"job1"}}]'
check "bash_kill"            "stopped job1"               "$shaman" run --yolo 'seq [{"tool":"bash","input":{"command":"sleep 60","background":true}},{"tool":"bash_kill","input":{"id":"job1"}}]'
check "task_output"          "echo: hello sub"            "$shaman" run 'seq [{"tool":"task","input":{"description":"d","prompt":"hello sub","subagent_type":"explore","background":true}},{"tool":"task_output","input":{"id":"bg1"}}]'
check "noninteractive env"   "GTP=0 PAGER=cat"            "$shaman" run --yolo 'call bash {"command":"echo GTP=$GIT_TERMINAL_PROMPT PAGER=$PAGER"}'
check "secret redacted"      "[REDACTED:api-key]"         "$shaman" run --yolo 'call bash {"command":"echo token sk-proj-abcdefghijklmnopqrstuvwxyz"}'
check "background subagent"  "started background subagent bg1" "$shaman" run 'call task {"description":"look","prompt":"hello sub","subagent_type":"explore","background":true}'
check "goal keeps going"     "goal not reached after 2"   env SHAMAN_CONFIG_CONTENT='{"goal_max_rounds":2}' "$shaman" run --goal "finish everything" hello
check "doctor"               "default model"              "$shaman" doctor
check "completion bash"      "complete -F _shaman shaman" "$shaman" completion bash
check "completion fish"      "complete -c shaman"         "$shaman" completion fish
check "worktree create"      "shaman/feat-x"              bash -c "git add -A >/dev/null 2>&1; git -c user.email=t@t -c user.name=t commit -qm wip >/dev/null 2>&1; '$shaman' worktree feat-x --plain hi 2>&1; git worktree list"
if command -v crontab >/dev/null; then
  check "schedule add/list"  "0 9 * * 1-5"                bash -c "'$shaman' schedule add '0 9 * * 1-5' 'daily notes' && '$shaman' schedule list"
fi
check "schedule bad cron"    "invalid cron"               "$shaman" schedule add "every day" "x"
check "read notebook"        "[output]"                   "$shaman" run 'call read {"filePath":"nb.ipynb"}'
check "read image to model"  "image_url"                  bash -c "'$shaman' run 'call read {\"filePath\":\"pic.png\"}' >/dev/null; tail -1 '$tmp/main.requests.jsonl'"

# --- providers and auth --------------------------------------------------------
export SHAMAN_CONFIG_CONTENT='{"provider":{"mockant":{"api":"anthropic","baseURL":"'"$url"'","apiKey":"k","models":{"claude-test":{"name":"Claude Test"}}}}}'
check "anthropic provider"   "anthropic says: the answer is 42" "$shaman" run -m mockant/claude-test read notes.txt
check "anthropic reasoning"  "hmm"                        "$shaman" run --reasoning -m mockant/claude-test hello
check "anthropic thinking kept" "(thinking kept)"         "$shaman" run --effort high -m mockant/claude-test read notes.txt
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
check "mcp prompt command"   "echo: Review cart.py focusing on rounding" "$shaman" run --command mock:review cart.py rounding
check "mcp resources list"   "memo://answer"              "$shaman" run --yolo 'call mcp_resources {}'
check "mcp server permission" "tool said: 3"             env SHAMAN_CONFIG_CONTENT='{"mcp":{"mock":{"command":["python3","'"$repo"'/tests/mock/mock_mcp.py"],"permission":{"*":"allow","fail":"deny"}}}}' "$shaman" run 'call mock_add {"a":1,"b":2}'
check "mcp tool deny beats yolo" "permission denied"       env SHAMAN_CONFIG_CONTENT='{"mcp":{"mock":{"command":["python3","'"$repo"'/tests/mock/mock_mcp.py"],"permission":{"*":"allow","fail":"deny"}}}}' "$shaman" run --yolo 'call mock_fail {}'
check "mcp resource read"    "tool said: the resource says 42" "$shaman" run --yolo 'call mcp_resources {"uri":"memo://answer"}'
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

check "accept edits mode"    "Create accepted.txt"       "$shaman" run --mode acceptEdits 'call write {"filePath":"accepted.txt","content":"x"}'
check "plan mode is read-only" "permission denied"        "$shaman" run --mode plan 'run touch nope.txt'
if command -v bwrap >/dev/null; then
  mkdir -p "$repo/build/sbtest"
  check "sandbox blocks writes" "Read-only file system"   env SHAMAN_CONFIG_CONTENT='{"sandbox":true,"permission":{"bash":"allow"}}' "$shaman" run "run touch $repo/build/sbtest/x"
  check "sandbox allows project" "tool said: ok"          env SHAMAN_CONFIG_CONTENT='{"sandbox":true,"permission":{"bash":"allow"}}' "$shaman" run 'run touch sandboxed.txt && echo ok'
  rm -rf "$repo/build/sbtest"
fi
# --- provider protocols and per-model tweaks --------------------------------------
export SHAMAN_CONFIG_CONTENT='{"provider":{"mockresp":{"api":"responses","baseURL":"'"$url"'","apiKeyCommand":"echo from-command","models":{"gpt-test":{"name":"GPT Test","reasoningEffort":"medium"}}}}}'
check "responses provider"   "responses says: the answer is 42" "$shaman" run -m mockresp/gpt-test --effort off read notes.txt
check "responses reasoning kept" "(reasoning kept)"       "$shaman" run -m mockresp/gpt-test read notes.txt
check "apiKeyCommand"        "mockresp (command)"         "$shaman" debug models
unset SHAMAN_CONFIG_CONTENT
check "model body and context" "top_k=5 cut=yes websearch=no" bash -c "SHAMAN_CONFIG_CONTENT='{\"provider\":{\"opencode\":{\"models\":{\"big-pickle\":{\"body\":{\"top_k\":5},\"context\":{\"maxToolOutput\":8},\"tools\":{\"websearch\":false}}}}}}' '$shaman' run -m opencode/big-pickle read notes.txt >/dev/null 2>&1; python3 -c \"
import json
reqs = [json.loads(l) for l in open('$tmp/main.requests.jsonl')]
last = reqs[-1]
tools = [t['function']['name'] for t in last.get('tools', [])]
cut = any('characters cut' in str(m.get('content', '')) for m in last['messages'])
print('top_k=%s cut=%s websearch=%s' % (last.get('top_k'), 'yes' if cut else 'no', 'yes' if 'websearch' in tools else 'no'))\""
printf 'class Cart:\n    def total(self):\n        return 1\n' > "$project/shop.py"
check "symbols find"         "shop.py:2  function total"  "$shaman" run 'call symbols {"operation":"find","query":"total"}'
check "debug index"          "symbols in"                 "$shaman" debug index Cart
# --- hooks ---------------------------------------------------------------------
check "hook blocks a tool"   "blocked by hook: no shell today" env SHAMAN_CONFIG_CONTENT='{"hooks":{"PreToolUse":[{"matcher":"bash","command":"echo no shell today >&2; exit 2"}]}}' "$shaman" run 'run echo hi'
check "hook gets tool json"  '"tool_response"'            bash -c "SHAMAN_CONFIG_CONTENT='{\"hooks\":{\"PostToolUse\":[{\"matcher\":\"glob\",\"command\":\"cat > $tmp/post.json\"}]}}' '$shaman' run 'call glob {\"pattern\":\"*.x\"}' >/dev/null 2>&1; cat '$tmp/post.json'"
check "hook adds context"    "HOOK-CTX-42"                env SHAMAN_CONFIG_CONTENT='{"hooks":{"UserPromptSubmit":[{"command":"echo HOOK-CTX-42"}]}}' "$shaman" run hello
check "hook rejects prompt"  "prompt blocked by hook: nope" env SHAMAN_CONFIG_CONTENT='{"hooks":{"UserPromptSubmit":[{"command":"echo nope >&2; exit 2"}]}}' "$shaman" run hello
check "stop hook continues"  "stop hook: run the tests (continuing)" env SHAMAN_CONFIG_CONTENT='{"hooks":{"Stop":[{"command":"test -f '"$tmp"'/stopped || { touch '"$tmp"'/stopped; echo run the tests >&2; exit 2; }"}]}}' "$shaman" run hello
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
check "server upload"        '"bytes":5'                  curl -s --noproxy '*' -X POST --data-binary 'hello' "$srv/upload?name=u.txt"
echo "before edit" > "$project/gui.txt"
check "server diff/revert/fork" "diff+ turns=1 restored forked" python3 -c "
import json, urllib.request as u
op = u.build_opener(u.ProxyHandler({}))
def call(m, p, b=None):
    r = op.open(u.Request('$srv' + p, method=m, data=json.dumps(b).encode() if b is not None else None, headers={'Content-Type': 'application/json'}))
    return r.read().decode()
s = json.loads(call('POST', '/session', {}))
seq = 'seq ' + json.dumps([{'tool': 'read', 'input': {'filePath': 'gui.txt'}}, {'tool': 'edit', 'input': {'filePath': 'gui.txt', 'oldString': 'before', 'newString': 'after'}}])
resp = op.open(u.Request('$srv/session/%s/message' % s['id'], method='POST', data=json.dumps({'text': seq}).encode(), headers={'Content-Type': 'application/json'}))
events, ev = '', ''
for line in resp:  # answer permission prompts like a client would
    line = line.decode(); events += line
    if line.startswith('event:'): ev = line[6:].strip()
    elif line.startswith('data:') and ev == 'permission': call('POST', '/permission/' + json.loads(line[5:])['id'], {'reply': 'once'})
out = ['diff+' if '+after edit' in events else 'nodiff']
turns = json.loads(call('GET', '/session/%s/turns' % s['id']))
out.append('turns=%d' % len(turns))
r = json.loads(call('POST', '/session/%s/revert' % s['id'], {'turn': 0}))
out.append('restored' if open('$project/gui.txt').read().startswith('before') and r.get('text', '').startswith('seq') else 'not-restored')
f = json.loads(call('POST', '/session/%s/fork' % s['id'], {}))
out.append('forked' if f.get('id') and f['id'] != s['id'] else 'no-fork')
print(' '.join(out))"
check "plan approval flow"   "approved build acceptEdits created" python3 -c "
import json, os, urllib.request as u
op = u.build_opener(u.ProxyHandler({}))
def call(m, p, b=None):
    return op.open(u.Request('$srv' + p, method=m, data=json.dumps(b).encode() if b is not None else None, headers={'Content-Type': 'application/json'})).read().decode()
s = json.loads(call('POST', '/session', {'agent': 'plan'}))
seq = 'seq ' + json.dumps([{'tool': 'plan_exit', 'input': {'plan': '1. create planned.txt'}}, {'tool': 'write', 'input': {'filePath': 'planned.txt', 'content': 'from the plan'}}])
resp = op.open(u.Request('$srv/session/%s/message' % s['id'], method='POST', data=json.dumps({'text': seq, 'agent': 'plan'}).encode(), headers={'Content-Type': 'application/json'}))
ev, out, done = '', [], {}
for line in resp:
    line = line.decode()
    if line.startswith('event:'): ev = line[6:].strip()
    elif line.startswith('data:'):
        d = json.loads(line[5:])
        if ev == 'question': call('POST', '/question/' + d['id'], {'answer': 'Yes, and auto-accept edits'})
        if ev == 'permission': out.append('asked-' + d['permission']); call('POST', '/permission/' + d['id'], {'reply': 'reject'})
        if ev == 'tool_end' and d['name'] == 'plan_exit' and 'approved' in d['output']: out.append('approved')
        if ev == 'done': done = d
out += [done.get('agent', '?'), done.get('mode', '?'), 'created' if os.path.exists('$project/planned.txt') else 'missing']
print(' '.join(out))"
check "server stream ends with a child running" "event: done" bash -c "id=\$(curl -s --noproxy '*' -X POST '$srv/session' -d '{}' | python3 -c 'import json,sys;print(json.load(sys.stdin)[\"id\"])');
  curl -sN --max-time 15 --noproxy '*' -X POST \"$srv/session/\$id/message\" -d '{\"text\":\"call bash {\\\"command\\\":\\\"tail -f /dev/null\\\",\\\"description\\\":\\\"long job\\\",\\\"background\\\":true}\"}'; pkill -f '^tail -f /dev/null$'"
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
check "serve --pair"         "token="                     bash -c "timeout 2 '$shaman' serve --pair --port 0 2>&1 | grep -E 'listening on http://0.0.0.0|/\?token=' | tail -1"
check "update notice in /info" '"update":"v9.9.9"'         bash -c "env -u SHAMAN_NO_UPDATE_CHECK SHAMAN_UPDATE_API='$url' SHAMAN_HOME='$tmp/uphome' '$shaman' serve --port 0 > '$tmp/up.out' 2>&1 & p=\$!; for i in \$(seq 50); do grep -q 'listening on' '$tmp/up.out' 2>/dev/null && break; sleep 0.1; done; sleep 1; curl -s --noproxy '*' \$(awk '/listening on/ {print \$5}' '$tmp/up.out')/info; kill \$p"
check "upgrade check"        "v9.9.9 is available"        env SHAMAN_UPDATE_API="$url" "$shaman" upgrade --check
check "tui smoke"            "tui ok"                     python3 "$repo/tests/e2e/tui_smoke.py" "$shaman"

start_mock limited --rate-limit big-pickle
export SHAMAN_ZEN_URL="$url"
check "free fallback"        "switching to opencode/space-bunny-free" "$shaman" run hi

echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
