---
name: ci-cd
description: Set up or fix CI/CD pipelines (GitHub Actions, GitLab CI) - builds, tests, caching, matrices, releases, deploys - and debug failing jobs. Use for workflow files and red builds.
---
# CI/CD

## Debugging a red job
1. Read the failing step's log from the first error, not the last line. Reproduce locally with the same command and versions.
2. Classify: code failure (fix the code), environment drift (pin versions), flaky test (fix the nondeterminism), infrastructure (retry once, then report).
3. Never "fix" CI by skipping, disabling or quarantining tests.

## Writing workflows (GitHub Actions)
- Trigger precisely (`push` to main, `pull_request`), with `concurrency` to cancel superseded runs.
- Pin actions to a version (or SHA for third-party actions); least-privilege `permissions:`.
- Cache dependencies keyed on the lockfile (`actions/setup-node` `cache: npm`, `actions/cache`).
- Matrix only what matters (OS × runtime versions you support); `fail-fast: false` for diagnosis.
- Fast feedback first: lint and unit tests before slow integration jobs; upload artifacts and test reports on failure.
- Secrets via `secrets.*`, never echoed; OIDC for cloud deploys instead of long-lived keys.
- Releases: build once, test the artifact, publish on tags; generate checksums.

## Verify
Validate syntax (`actionlint` if available), push to a branch, and watch the run before merging.
