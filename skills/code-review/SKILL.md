---
name: code-review
description: Review a diff, branch or PR for real bugs, security holes and maintainability problems, ranked by severity. Use when asked to review, audit or check changes.
---
# Code review

Goal: find the problems that would hurt in production, not restyle the code.

## Gather
1. Get the change: `git diff` (unstaged), `git diff --cached`, `git diff <base>...HEAD`, or the PR diff the user gave.
2. For each changed file, read enough surrounding code to know what the change interacts with: callers, callees, tests, config.
3. Find the intent: commit messages, PR description, linked issue. Review against the intent, not against your taste.

## Look for, in this order
1. **Correctness**: off-by-one, wrong conditions, inverted logic, unhandled `null`/empty/error cases, wrong units, integer overflow, float equality, time zones, encoding.
2. **State and concurrency**: races, shared mutable state, missing locks, async ordering, retries that are not idempotent, resource leaks (files, sockets, goroutines, listeners).
3. **Security**: injection (SQL, shell, path, template), missing authz on new endpoints, secrets in code or logs, unsafe deserialization, SSRF, weak randomness, over-broad CORS or permissions.
4. **Contracts**: API or schema changes that break callers, migrations without backfill, changed defaults, removed fields still read elsewhere (grep for them).
5. **Tests**: does a test fail without this change? Are edge cases covered? Are mocks hiding the bug?
6. **Maintainability** (only if it matters): duplicated logic that will drift, misleading names, dead code, missing comments on non-obvious behaviour.

## Verify before reporting
- Reproduce or reason concretely: "with input X, line N does Y, so Z happens." Drop anything you can't make concrete.
- Grep for other call sites before claiming something breaks them.
- Run the tests or build if you can.

## Report
- Most severe first. Each finding: `path:line`, one-sentence problem, concrete failure scenario, suggested fix.
- Label severity: **blocker**, **should fix**, **nit**. Keep nits few.
- If it looks good, say so plainly and mention what you checked.
