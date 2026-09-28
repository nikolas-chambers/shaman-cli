---
name: shell-scripting
description: Write robust, portable shell scripts (bash, POSIX sh, PowerShell) - safe quoting, error handling, argument parsing, idempotency. Use for automation scripts, CI steps and one-off tooling.
---
# Shell scripting

## Defaults for bash
```bash
#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'
```
- Quote every expansion: `"$var"`, `"${array[@]}"`, `"$(cmd)"`.
- `[[ ... ]]` for tests in bash; `[ ... ]` with quoted operands in POSIX sh.
- `local` variables in functions; `readonly` for constants.
- `trap 'cleanup' EXIT` for temp files (`tmp=$(mktemp -d)`).
- Check dependencies up front: `command -v jq >/dev/null || { echo "jq required" >&2; exit 1; }`.
- Errors and diagnostics to stderr (`>&2`); meaningful exit codes.

## Structure
- A `usage()` function and argument parsing with `getopts` (or a `while case` loop for long options).
- Make scripts idempotent: safe to run twice (check before creating, `mkdir -p`, `ln -sfn`).
- Prefer `find ... -print0 | xargs -0` or `while IFS= read -r -d '' f` for filenames with spaces.
- Don't parse `ls`; use globs or `find`.

## Portability
- `#!/bin/sh` scripts must avoid bashisms (arrays, `[[`, `source`, `$'...'`); check with `shellcheck -s sh` or `dash`.
- GNU vs BSD tools differ (`sed -i ''` on macOS, `date`, `stat`, `readlink -f`); detect or avoid.
- For Windows, write PowerShell (`$ErrorActionPreference = 'Stop'`) rather than batch files.

## Verify
Run `shellcheck` if available, test with paths containing spaces and empty inputs, and run twice to confirm idempotency.
