---
name: release-management
description: Prepare and ship releases - semantic versioning, changelogs, tags, release notes, build artifacts, rollback plans. Use when cutting a release, writing release notes or setting up a release process.
---
# Release management

1. **Decide the version** (SemVer): breaking change → major; new feature → minor; fixes → patch. Pre-releases as `1.4.0-rc.1`.
2. **Collect changes** since the last tag: `git log --oneline $(git describe --tags --abbrev=0)..HEAD`, merged PRs, and issues closed.
3. **Changelog** (Keep a Changelog style): Added / Changed / Deprecated / Removed / Fixed / Security, written for users, with migration steps for breaking changes.
4. **Bump versions** everywhere they live (manifests, `--version` output, docs) in one commit: `Release vX.Y.Z`.
5. **Verify**: clean build from a fresh clone, full test suite, smoke-test the built artifact (install it and run it).
6. **Tag and publish**: annotated tag `git tag -a vX.Y.Z -m "vX.Y.Z"`, push the tag; CI builds artifacts, checksums and the release page.
7. **Release notes**: highlights first (2-5 bullets), then the changelog, upgrade notes, and thanks to contributors.
8. **Rollback plan**: how to revert (previous artifact, feature flag, down-migration) before you need it. Watch error rates after release.
