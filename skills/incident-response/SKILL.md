---
name: incident-response
description: Handle a production incident methodically - assess impact, mitigate first, find the cause from logs/metrics/deploys, communicate, and write the postmortem. Use when something is broken or degraded in production.
---
# Incident response

## 1. Stabilise first, investigate second
- Establish impact: who is affected, since when, how badly (errors, latency, data).
- Look at what changed: recent deploys, config/feature-flag changes, dependency or infrastructure events, traffic spikes.
- Mitigate with the safest reversible action: roll back the last deploy, disable the flag, scale out, fail over, rate-limit. A mitigated incident with an unknown cause beats a known cause still burning.
- Get confirmation from the user before any production-changing command.

## 2. Diagnose
- Correlate timelines: error-rate/latency graphs vs deploy times vs log events.
- Narrow scope: one region/endpoint/customer/version? Compare healthy vs unhealthy instances.
- Read logs around the first errors, not the loudest ones. Follow a single failing request by trace/request id.
- Form hypotheses and check each against data (see the debugging skill).

## 3. Communicate
Short, regular updates: what's impacted, what's being done, next update time. Facts, not guesses.

## 4. Afterwards
Blameless postmortem (see technical-writing): timeline, root cause and contributing factors, what detection missed, action items with owners — especially ones that make the next incident shorter (alerts, runbooks, safer deploys).
