---
name: api-design
description: Design or review HTTP/JSON, RPC or library APIs - naming, resources, errors, pagination, versioning, idempotency, compatibility. Use when adding endpoints, public functions or SDK surfaces.
---
# API design

An API is a promise; design it for the caller and for change.

## Shape
- Model resources and actions the caller thinks in, not internal tables. Consistent nouns, plural collections, predictable nesting.
- HTTP semantics: GET safe, PUT/DELETE idempotent, POST for creation or actions; correct status codes (400 vs 401 vs 403 vs 404 vs 409 vs 422 vs 429).
- Consistent casing and date formats (ISO 8601, UTC), explicit units, stable IDs as strings.

## Errors
One error shape everywhere: machine-readable `code`, human `message`, optional `details`/field errors, and a request id. Never leak stack traces or internals.

## Collections
Paginate from day one (cursor-based for large or changing data), cap page size, support filtering and sorting explicitly, return totals only if cheap.

## Safety
- Idempotency keys for create/payment-like operations; safe retries.
- Validate input strictly at the boundary; reject unknown fields if the ecosystem allows.
- Rate limits with `Retry-After`; timeouts documented.

## Evolution
- Additive changes only within a version: new optional fields, new endpoints.
- Deprecate before removing; version when a breaking change is unavoidable.
- Libraries: small public surface, explicit exports, semantic versioning.

## Deliverable
Write the spec (OpenAPI, protobuf, or typed signatures) and example requests/responses before or alongside the implementation.
