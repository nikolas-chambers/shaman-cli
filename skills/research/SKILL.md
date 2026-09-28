---
name: research
description: Research a technical question across the web, documentation and the codebase, compare options, and deliver a sourced recommendation. Use for "which library/approach", "how does X work", or investigating unfamiliar tech.
---
# Research

1. **Frame it**: the decision to make, the constraints (language, licence, scale, budget), and what would change the answer.
2. **Search wide, then deep**: `websearch` several phrasings; `webfetch` official docs, changelogs, issue trackers and benchmarks; `grep`/`read` the local codebase for how things are done today. Delegate independent threads to `task` subagents in parallel to keep your own context focused.
3. **Judge sources**: prefer primary sources (official docs, source code, maintainers) and recent dates; note when a claim comes from a single blog post. Check versions: advice for v1 may be wrong for v3.
4. **Verify what you can**: run a tiny proof of concept or check the API exists in the installed version.
5. **Deliver**: recommendation first; a comparison table of the real options on the criteria that matter; risks and unknowns; links to sources for every non-obvious claim.
