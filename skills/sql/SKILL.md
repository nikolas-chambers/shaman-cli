---
name: sql
description: Write, optimise and review SQL - queries, joins, window functions, indexes, EXPLAIN plans, transactions - for Postgres, MySQL, SQLite and others. Use for database queries, slow queries and schema questions.
---
# SQL

## Before writing
- Identify the engine and version (syntax differs: `LIMIT` vs `TOP`, `ILIKE`, JSON operators, upsert syntax).
- Read the schema: tables, keys, indexes, row counts (`\d table` in psql, `SHOW CREATE TABLE`, `.schema`).

## Writing queries
- Select explicit columns, never `SELECT *` in application code.
- Join on keys with explicit `JOIN ... ON`; check row counts before/after joins to catch fan-out.
- Aggregates: every non-aggregated column is in `GROUP BY`; filter groups with `HAVING`.
- Window functions for rankings, running totals and "latest per group": `ROW_NUMBER() OVER (PARTITION BY user_id ORDER BY created_at DESC)`.
- NULL logic: `NOT IN` with NULLs returns nothing (use `NOT EXISTS`); `COUNT(col)` skips NULLs; use `COALESCE` deliberately.
- Parameterise everything that comes from users. Never concatenate input into SQL.
- CTEs (`WITH`) for readability; check whether the engine inlines or materialises them.

## Performance
1. `EXPLAIN (ANALYZE, BUFFERS)` (Postgres) / `EXPLAIN ANALYZE` (MySQL 8) on realistic data.
2. Look for sequential scans on big tables, nested loops over large inputs, sorts spilling to disk, row-estimate mismatches.
3. Index the filter/join/sort columns, in the right order (equality columns first, then range); consider partial and covering indexes.
4. Avoid functions on indexed columns in `WHERE` (`WHERE date(created_at) = ...` → range condition).
5. Paginate with keyset (`WHERE (created_at, id) < (?, ?) ORDER BY ... LIMIT n`) instead of large `OFFSET`s.
6. Re-run `EXPLAIN ANALYZE` and compare timings.

## Safety
Wrap multi-statement changes in a transaction; run destructive `UPDATE`/`DELETE` as a `SELECT` with the same `WHERE` first and check the count; never run writes against production without the user's say-so.
