---
name: dependency-upgrade
description: Upgrade libraries, frameworks, toolchains or runtimes safely - read changelogs, handle breaking changes, keep lockfiles consistent, verify. Use for version bumps and security patches.
---
# Dependency upgrades

## Plan
- Inventory: current versions, target versions, and why (security fix, feature, EOL). Check the lockfile, not just the manifest.
- Read release notes and migration guides for every major version crossed. List breaking changes that apply to this codebase (grep for affected APIs).
- Upgrade one dependency (or one tightly coupled group) at a time.

## Do
- Use the package manager to change versions so lockfiles stay consistent (`npm install x@y`, `cargo update -p`, `uv lock --upgrade-package`, `go get x@v`). Never hand-edit lockfiles.
- Apply codemods where the project provides them; fix remaining breakages by hand.
- Remove shims or workarounds that the new version makes unnecessary.

## Verify
- Build, type-check, lint, run the full test suite.
- Exercise the areas that use the dependency most heavily.
- Check bundle size / binary size / startup time if relevant, and new transitive dependencies or install scripts.

## Report
What changed, breaking changes handled, anything deferred, and how it was verified. Commit each upgrade separately.
