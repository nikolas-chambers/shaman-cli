#!/usr/bin/env python3
"""Scaffold or validate a shaman skill.

  new_skill.py <name> [--dir .shaman/skills] [--description "..."]
  new_skill.py --check <skill-dir>
"""
import argparse
import os
import re
import sys

TEMPLATE = """---
name: {name}
description: {description}
---
# {title}

## When to use
Describe the situations and trigger words.

## Steps
1. ...
2. ...

## Verify
- ...
"""


def check(path):
    problems = []
    skill = os.path.join(path, "SKILL.md")
    if not os.path.isfile(skill):
        return [f"{skill} not found"]
    text = open(skill, encoding="utf-8").read()
    m = re.match(r"^---\n(.*?)\n---\n(.*)$", text, re.S)
    if not m:
        return ["SKILL.md must start with --- front matter ---"]
    fields = dict(line.split(":", 1) for line in m.group(1).splitlines() if ":" in line)
    fields = {k.strip(): v.strip() for k, v in fields.items()}
    if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", fields.get("name", "")):
        problems.append("name must be lowercase letters, digits and dashes")
    desc = fields.get("description", "")
    if len(desc) < 40:
        problems.append("description is too short: say what the skill does and when to use it")
    if len(desc) > 600:
        problems.append("description is over 600 characters; move detail into the body")
    if "when" not in desc.lower() and "use " not in desc.lower():
        problems.append("description should say when to use the skill")
    body = m.group(2)
    if len(body.splitlines()) > 300:
        problems.append("body is long; move reference material into references/*.md")
    for ref in re.findall(r"`((?:scripts|references|templates)/[^`\s]+)`", body):
        if "*" not in ref and not os.path.exists(os.path.join(path, ref.split()[0])):
            problems.append(f"referenced file missing: {ref}")
    if "TODO" in text or "\n1. ...\n" in text:
        problems.append("template placeholders remain")
    return problems


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("name", nargs="?")
    p.add_argument("--dir", default=".shaman/skills")
    p.add_argument("--description", default="What this skill does. Use when ...")
    p.add_argument("--check", metavar="SKILL_DIR")
    a = p.parse_args()
    if a.check:
        problems = check(a.check)
        for x in problems:
            print("-", x)
        print("ok" if not problems else f"{len(problems)} problem(s)")
        sys.exit(1 if problems else 0)
    if not a.name:
        p.error("name is required")
    path = os.path.join(a.dir, a.name)
    if os.path.exists(path):
        sys.exit(f"{path} already exists")
    os.makedirs(os.path.join(path, "scripts"))
    with open(os.path.join(path, "SKILL.md"), "w", encoding="utf-8") as f:
        f.write(TEMPLATE.format(name=a.name, description=a.description, title=a.name.replace("-", " ").title()))
    print(f"created {path}/SKILL.md; fill it in, then run: {sys.argv[0]} --check {path}")


if __name__ == "__main__":
    main()
