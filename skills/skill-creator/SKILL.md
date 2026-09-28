---
name: skill-creator
description: Create, improve and test shaman skills - folders with a SKILL.md and optional scripts that teach the agent a repeatable task. Use when the user wants to capture a workflow, house style or procedure as a skill.
---
# Creating skills

A skill packages know-how the agent loads only when relevant: a `SKILL.md` with front matter plus optional
`scripts/`, `templates/` and `references/`. Project skills live in `.shaman/skills/<name>/`; personal ones in
`~/.config/shaman/skills/<name>/`. Folders may be nested for grouping.

## Steps
1. **Interview**: what task, what inputs and outputs, what "good" looks like, examples of past results, and the mistakes to avoid. Get one or two concrete examples.
2. **Scaffold**: `scripts/new_skill.py <name> --dir .shaman/skills --description "..."`.
3. **Write the description** (front matter): it is the only part the agent sees before loading, so state *what it does* and *when to use it*, including trigger words ("Use when ... mentions .xlsx, spreadsheet, ..."). One or two sentences.
4. **Write the body**: imperative, concrete steps; commands to run; decision rules; a short checklist for verifying the result. Put long reference material in `references/*.md` and link to it instead of inlining.
5. **Add scripts** for anything deterministic or fiddly (parsing, file formats, API calls). Scripts beat prose: they are testable and cheap in context. Print clear errors and usage.
6. **Validate**: `scripts/new_skill.py --check <skill-dir>` checks the front matter, description length and referenced files.
7. **Test for real**: `shaman debug skills` should list it; then run a realistic request with `shaman run` and see that the agent loads the skill (`> Skill <name>`) and follows it. Iterate on the description if it isn't picked up, and on the body if the result is wrong.

## Good skills
- Narrow and specific beats broad ("quarterly board report in our template", not "writing").
- Show the exact commands; name the files; state defaults.
- Keep `SKILL.md` under ~150 lines; move depth into references.
- No secrets in skills. Read credentials from the environment.
