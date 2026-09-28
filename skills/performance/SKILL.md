---
name: performance
description: Diagnose and fix slowness, high memory or CPU use with measurements first - profiling, benchmarks, query analysis - instead of guesses. Use when something is slow or resource-hungry.
---
# Performance

Measure, change one thing, measure again.

## 1. Define the problem
- Which operation, how slow, under what load and data size, and what target is acceptable.
- Build a repeatable benchmark or timing script before changing anything. Record the baseline.

## 2. Find where the time goes
- Profile, don't guess: `perf`, `py-spy`, `cProfile`, Chrome/Node profiler, `pprof`, `Instruments`, database `EXPLAIN ANALYZE`, request tracing.
- Typical culprits: N+1 queries, missing indexes, unbounded result sets, repeated work in loops, synchronous I/O on hot paths, excessive allocation/copying, chatty network calls, lock contention, JSON (de)serialisation of huge payloads.

## 3. Fix the biggest cost first
- Algorithmic wins beat micro-optimisations: better data structure, batching, caching with clear invalidation, streaming instead of loading everything.
- Push work to the database when it is good at it (filters, joins, aggregates) with the right indexes.
- Keep the code readable; comment why an optimisation exists.

## 4. Verify
Re-run the benchmark, compare against the baseline with numbers, run the tests, and check you did not just move the cost elsewhere (memory, latency tail, cold start).
