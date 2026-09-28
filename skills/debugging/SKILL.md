---
name: debugging
description: Systematically find the root cause of a bug, crash, failing test or wrong output instead of guessing. Use when something is broken and the cause is not obvious.
---
# Debugging

Never patch a symptom you do not understand. Work from evidence to cause.

## 1. Reproduce
- Get an exact, repeatable reproduction: command, input, environment. Write it down.
- Shrink it: smallest input, fewest steps, one test. A fast repro makes every later step cheap.
- If you cannot reproduce, gather evidence instead (logs, stack traces, versions, config) and say so.

## 2. Read the evidence
- Read the full error and stack trace. The first frame in *our* code is usually where to start.
- Note what changed recently: `git log -p --since=...`, dependency bumps, config, data.

## 3. Form hypotheses, then test them
- List 2-3 plausible causes. For each, name an observation that would confirm or kill it.
- Test the cheapest one first: add a log line, print a value, run under a debugger, check an assumption with a one-liner.
- `git bisect run <test>` when "it used to work" and history is available.
- Change one thing at a time. Revert experiments that did not help.

## 4. Find the root cause
- Keep asking "why did that happen?" until the answer is a mistake in code, data or config, not "it crashed".
- Check for siblings: grep for the same pattern elsewhere; the bug is rarely unique.

## 5. Fix and prove it
- Write a failing test that captures the bug, then make it pass.
- Fix the cause, not the symptom (no blanket `try/catch`, no sleeps to hide races).
- Run the wider test suite. Remove temporary logging.

## Report
Cause (with `path:line`), why it happened, the fix, and how you verified it. Mention related risks you noticed.
