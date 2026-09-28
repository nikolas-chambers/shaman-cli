#!/usr/bin/env python3
"""Summarise a repository: languages, size, layout, manifests, entry points, tests, CI.

  repo_map.py [path]
"""
import collections
import os
import sys

SKIP = {".git", "node_modules", "vendor", "dist", "build", "target", ".venv", "venv", "__pycache__", ".next", ".cache", "out", "bin", "obj"}
LANG = {".py": "Python", ".js": "JavaScript", ".jsx": "JavaScript", ".ts": "TypeScript", ".tsx": "TypeScript", ".go": "Go", ".rs": "Rust",
        ".java": "Java", ".kt": "Kotlin", ".c": "C", ".h": "C/C++", ".cc": "C++", ".cpp": "C++", ".hpp": "C++", ".cs": "C#", ".rb": "Ruby",
        ".php": "PHP", ".swift": "Swift", ".scala": "Scala", ".sh": "Shell", ".lua": "Lua", ".zig": "Zig", ".vue": "Vue", ".svelte": "Svelte",
        ".sql": "SQL", ".dart": "Dart", ".ex": "Elixir", ".exs": "Elixir", ".hs": "Haskell", ".ml": "OCaml"}
MANIFESTS = {"package.json", "pyproject.toml", "setup.py", "requirements.txt", "Cargo.toml", "go.mod", "pom.xml", "build.gradle",
             "build.gradle.kts", "CMakeLists.txt", "Makefile", "meson.build", "Gemfile", "composer.json", "mix.exs", "deno.json",
             "Dockerfile", "docker-compose.yml", "compose.yaml", "flake.nix", "pubspec.yaml", "*.csproj", "*.sln"}
ENTRY = {"main.py", "__main__.py", "app.py", "manage.py", "server.py", "main.go", "main.rs", "lib.rs", "main.cpp", "main.c",
         "index.js", "index.ts", "server.js", "server.ts", "app.js", "app.ts", "Program.cs", "Main.java"}


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    langs, files, lines = collections.Counter(), 0, collections.Counter()
    manifests, entries, tests, ci = [], [], 0, []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP and not d.startswith(".") or d in (".github", ".gitlab")]
        rel = os.path.relpath(dirpath, root)
        for f in filenames:
            path = os.path.join(rel, f) if rel != "." else f
            files += 1
            ext = os.path.splitext(f)[1].lower()
            if ext in LANG:
                langs[LANG[ext]] += 1
                try:
                    with open(os.path.join(dirpath, f), "rb") as fh:
                        lines[LANG[ext]] += sum(1 for _ in fh)
                except OSError:
                    pass
            if f in MANIFESTS or ext in (".csproj", ".sln"):
                manifests.append(path)
            if f in ENTRY:
                entries.append(path)
            if "test" in f.lower() or "/tests/" in "/" + path or "spec" in f.lower():
                tests += 1
            if ".github/workflows" in path or ".gitlab-ci" in f or f in ("Jenkinsfile", ".circleci", "azure-pipelines.yml"):
                ci.append(path)
    print(f"repo: {root}\nfiles: {files}")
    print("languages (files, lines):")
    for lang, n in langs.most_common(8):
        print(f"  {lang:<12} {n:>6} files {lines[lang]:>9} lines")
    print("top level:")
    for entry in sorted(os.listdir(root)):
        if entry in SKIP or entry.startswith(".") and entry not in (".github",):
            continue
        kind = "/" if os.path.isdir(os.path.join(root, entry)) else ""
        print(f"  {entry}{kind}")
    print("manifests / build:", ", ".join(sorted(manifests)[:20]) or "none found")
    print("likely entry points:", ", ".join(sorted(entries)[:15]) or "none found")
    print(f"test files: {tests}")
    print("CI:", ", ".join(ci[:10]) or "none found")
    for doc in ("README.md", "CONTRIBUTING.md", "AGENTS.md", "SHAMAN.md", "docs"):
        if os.path.exists(os.path.join(root, doc)):
            print(f"read: {doc}")


if __name__ == "__main__":
    main()
