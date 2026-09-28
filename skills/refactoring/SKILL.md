---
name: refactoring
description: Restructure code without changing behaviour - extract, rename, split, dedupe, simplify - in small verified steps. Use for cleanup, tech debt and preparing code for a new feature.
---
# Refactoring

Refactoring changes structure, never behaviour. Prove that at every step.

## Prepare
- Make sure tests cover the code you will touch. If not, add characterisation tests that pin current behaviour first, even if the behaviour looks wrong.
- Know every caller: grep, the lsp tool (`references`), and check reflection/config/string-based lookups.

## Work in small steps
- One transformation at a time: rename, extract function, inline, move, split module, replace conditional with polymorphism, introduce parameter object.
- Build and run tests after each step. If something breaks, undo that step instead of debugging a pile of changes.
- Keep public interfaces stable, or change them with a deprecation path.

## Judgement
- Remove duplication only when the copies change for the same reason.
- Prefer clear, boring code over clever abstractions. Don't add layers "for the future".
- Don't mix refactoring with feature work or bug fixes in the same commit.

## Finish
Summarise what moved where and why, confirm tests pass, and flag any behaviour you intentionally preserved that looks like a bug.
