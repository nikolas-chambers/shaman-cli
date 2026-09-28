---
name: technical-writing
description: Write clear technical prose - design docs, RFCs, ADRs, runbooks, incident reports, emails and release announcements - with structure and precision. Use when asked to write or improve any explanatory document.
---
# Technical writing

## Principles
- Start with the conclusion or the ask; background after.
- One idea per paragraph; short sentences; concrete nouns and verbs; numbers with units.
- Define terms once, then use them consistently. Cut filler ("basically", "in order to", "it should be noted").
- Prefer lists and tables for comparisons and steps; prose for reasoning.

## Templates
**Design doc / RFC**: Summary · Problem and goals (and non-goals) · Proposal · Alternatives considered (with why not) · Risks and mitigations · Rollout and testing · Open questions.
**ADR**: Title · Status · Context · Decision · Consequences.
**Runbook**: Symptom · Impact · Diagnosis steps (commands) · Mitigation · Escalation · Links.
**Incident report**: Summary · Timeline (UTC) · Impact · Root cause · What went well / badly · Action items with owners and dates. Blameless.
**Status update / email**: TL;DR line · What changed · What's next · Blockers/asks.

## Process
1. Know the reader and what they must decide or do.
2. Outline headings first; get agreement on structure for long docs.
3. Draft, then cut 20%. Check every claim, command and link.
4. Read it as the reader: what question is still unanswered?
