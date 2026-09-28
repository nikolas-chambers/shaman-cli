---
name: git-workflow
description: Stage, commit, branch, write commit messages and pull request descriptions, and resolve merge conflicts safely. Use for any git or PR task.
---
# Git workflow

## Safety first
- Check state before acting: `git status`, `git branch --show-current`, `git log --oneline -5`.
- Never rewrite shared history (no `push --force`, `rebase` or `commit --amend` on branches others use) unless the user asks.
- Never commit secrets, build output or large binaries. Check `git diff --cached` before committing.
- Don't `git add -A` blindly; stage what belongs to this change.

## Commit messages
```
Short imperative summary, max ~65 chars

Why the change is needed and what it does, wrapped at ~72 columns.
Mention user-visible effects, trade-offs and follow-ups.
```
- One logical change per commit. Separate refactors from behaviour changes.
- Follow the repo's convention if it has one (Conventional Commits, ticket prefixes): check `git log`.

## Pull requests
- Title: the same imperative summary.
- Body: **What** changed, **Why**, **How to test**, risks or rollout notes, screenshots for UI.
- Use the repo's PR template if one exists.
- Keep PRs small enough to review; split when mixing unrelated changes.

## Merge conflicts
1. `git status` to list conflicted files; read both sides and the common ancestor (`git diff --base`, `git log --merge`).
2. Resolve by understanding intent on both sides, not by picking one wholesale.
3. Regenerate lockfiles and generated code with their tools instead of hand-editing.
4. Build and run tests before completing the merge.
