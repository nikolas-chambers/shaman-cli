---
name: observability
description: Add or improve logging, metrics, tracing and alerting so production problems can be found fast - structured logs, RED/USE metrics, OpenTelemetry, useful alerts. Use when instrumenting code or investigating how to monitor a system.
---
# Observability

## Logs
- Structured (JSON) with consistent fields: `timestamp`, `level`, `message`, `service`, `request_id`/`trace_id`, plus event-specific keys.
- Log events and decisions, not every line; `error` means someone should act; include the context needed to reproduce.
- Never log secrets, tokens, full card numbers or unnecessary personal data. Redact at the logger.

## Metrics
- Services: RED — **R**ate, **E**rrors, **D**uration (histograms, not averages) per endpoint.
- Resources: USE — **U**tilisation, **S**aturation, **E**rrors (CPU, memory, queues, pools).
- Keep label cardinality bounded (no user IDs or raw URLs as labels).

## Traces
Instrument with OpenTelemetry: propagate context across HTTP/queue boundaries, name spans after operations, record errors on spans, sample sensibly.

## Alerts
Alert on symptoms users feel (error rate, latency SLO burn, saturation about to hit a wall), not on every cause. Each alert has an owner, a runbook link and a clear action. Test alerts fire.

## Verify
Trigger the code path locally or in staging and confirm the log line, metric and span appear with the expected fields.
