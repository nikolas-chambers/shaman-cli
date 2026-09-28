---
name: migrations
description: Change database schemas and data safely in production - expand/contract, backfills, zero-downtime deploys, rollbacks. Use for any schema or data migration.
---
# Database migrations

Assume the old and new code run at the same time against the same database.

## Expand / contract
1. **Expand**: add new columns/tables/indexes in a backward-compatible way (nullable or with defaults; no renames or drops).
2. **Migrate code**: write to both old and new, read from new with fallback; deploy.
3. **Backfill**: move existing data in batches, idempotently, with progress logging and throttling.
4. **Contract**: once nothing reads the old shape, remove it in a later deploy.

## Safety checks
- Large tables: create indexes concurrently (Postgres `CREATE INDEX CONCURRENTLY`), avoid long locks, avoid full-table rewrites (`ALTER ... SET DEFAULT` on huge tables in old MySQL versions, type changes).
- Every migration has a tested down/rollback path or an explicit note why not.
- Keep schema and data migrations separate. Never mix a migration with unrelated code changes.
- Test on a production-sized copy when possible; measure lock time and duration.

## Deliverable
The migration files (in the project's migration tool), the code changes per phase, the backfill script, and a short rollout plan: order of deploys, how to verify, how to roll back.
