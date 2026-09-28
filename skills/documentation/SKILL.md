---
name: documentation
description: Write or improve READMEs, guides, API docs, docstrings and changelogs that are accurate, concise and task-oriented. Use when asked to document code or a project.
---
# Documentation

Good docs answer the reader's next question quickly and are true.

## Before writing
- Identify the reader (new user, contributor, operator, API consumer) and what they are trying to do.
- Verify every command and example by running it or reading the code. Wrong docs are worse than none.

## README shape
1. One or two sentences: what it is and why someone would use it.
2. Quick start that works copy-pasted: install, minimal config, first command, expected output.
3. Common tasks, each as a short heading with a command or snippet.
4. Configuration reference (table or annotated example).
5. Links to deeper docs, contributing and license.

## Style
- Short sentences, active voice, concrete examples. Lead with the answer.
- Code blocks for anything the reader types; show output when it helps.
- Prefer tables for options and reference data, prose for concepts.
- Don't document the obvious; do document surprises, limits and failure modes.

## Code-level docs
Docstrings explain *why* and the contract (inputs, outputs, errors, side effects), not a restatement of the code. Match the project's docstring format.

## Changelogs
Group by Added / Changed / Fixed / Removed, written for users, newest first, with migration notes for breaking changes.
