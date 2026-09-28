---
name: codebase-onboarding
description: Understand an unfamiliar codebase fast and explain it - architecture, entry points, data flow, build/test commands, conventions - producing a concise map. Use for "explain this repo", onboarding, or before a large change in unknown code.
---
# Codebase onboarding

1. **Map it**: `python3 scripts/repo_map.py .` prints languages, size, top-level layout, build/manifests, entry points, tests and CI files.
2. **Read the obvious docs**: README, CONTRIBUTING, docs/, AGENTS.md/SHAMAN.md, ADRs, the CI workflow (it shows the real build and test commands).
3. **Find the spine**: entry points (`main`, server bootstrap, CLI parser, routes), then follow one representative request or command end to end with `grep`/`lsp definition`. Use `task` explore subagents for parallel questions.
4. **Build and test it** to confirm the commands actually work; note anything broken.
5. **Explain** in this shape:
   - What it is (2 sentences) and the main components with their directories.
   - How a request/command flows through the code (numbered steps with `path:line`).
   - How to build, run, test (exact commands).
   - Conventions (error handling, naming, testing style) and gotchas.
   - Where to start for the user's specific goal.
6. Offer to save the essentials to `.shaman/MEMORY.md` (memory tool) or an AGENTS.md so future sessions start informed.
