---
name: llm-apps
description: Build features on top of language models - prompt design, structured output, tool calling, RAG, evaluation, cost and latency control, safety. Use when adding AI features, chatbots, agents or model integrations to an app.
---
# Building with LLMs

## Prompts
- System prompt: role, task, constraints, output format, and what to do when unsure. Put stable instructions first (better prompt caching).
- Give examples (few-shot) for formats; delimit inputs clearly (`<document>...</document>`).
- Ask for structured output (JSON schema / tool call) instead of parsing prose; validate it and retry on failure.

## Tools and agents
- Few, well-described tools with strict JSON Schemas; return concise, model-readable results; errors that say how to recover.
- Bound loops (max steps, timeouts) and log every step for debugging.

## Retrieval (RAG)
Chunk by structure (headings, functions), embed, retrieve top-k with metadata filters, rerank, and cite sources in the answer. Evaluate retrieval separately from generation.

## Evaluation
Build a small eval set early (real inputs + expected properties), score automatically (exact match, schema validity, LLM-as-judge with a rubric), and run it on every prompt/model change. Track regressions, not vibes.

## Cost and latency
Pick the smallest model that passes the evals; cache prompts and responses; stream output; cap max tokens; batch offline work.

## Safety
Treat model output as untrusted input (never execute or render it unsanitised); guard against prompt injection from retrieved content; keep secrets out of prompts; add human confirmation for irreversible actions.
