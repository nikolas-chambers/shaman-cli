---
name: testing
description: Write or improve automated tests that catch real regressions - unit, integration and end-to-end - following the project's existing framework and style. Use when adding tests, fixing flaky ones or raising coverage.
---
# Testing

## Before writing
- Find the existing test setup: framework, directory layout, naming, fixtures, how to run one test. Follow it exactly.
- Decide the level: pure logic → unit; boundaries (DB, HTTP, filesystem) → integration; user journeys → a few end-to-end tests.

## What to test
- The behaviour the code promises, through its public interface, not its private details.
- Edge cases: empty, one, many; zero and negative; max sizes; unicode; missing/extra fields; time boundaries; errors from dependencies.
- Every bug you fix: add the test that would have caught it, and watch it fail first.

## How to write them
- One behaviour per test, named for it: `rejects_expired_token`, not `test2`.
- Arrange / act / assert, with the expected value written literally when possible.
- Deterministic: control time, randomness, ordering and network. No sleeps; wait on conditions.
- Prefer real collaborators or fakes over deep mocks. Mock at the boundary you do not own.
- Keep fixtures small and local to the test that uses them.

## Flaky tests
Find the nondeterminism (time, order, shared state, concurrency, network) and remove it. Never "fix" flakiness with retries or longer timeouts alone.

## Finish
Run the new tests and the surrounding suite. Report what is covered, what is deliberately not, and the command to run them.
