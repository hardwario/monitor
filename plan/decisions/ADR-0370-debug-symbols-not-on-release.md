# ADR-0370 — Debug symbols are not release assets

- **Status:** Accepted (supersedes ADR-0232; amends ADR-0125)
- **Date:** 2026-11-13
- **Decided by:** Matej Strnad (rc.4 release review)

## Context

ADR-0232 kept `hwio-monitor-<version>-debug-symbols-<os>.zip` on every GitHub release, listed in `SHA256SUMS`
and covered by the attestations, so that a crash report of a release build can be symbolized for as long as the
release exists. After publishing v2.0.0-rc.4 the user asked to remove them: users do not need them, and three
large archives next to four installers make the download list harder to read.

## Options considered

1. Keep them on the release (ADR-0232).
2. Remove them from the release and keep them only as workflow artifacts of the release run.
3. Remove them and store them in a separate long-term place (private storage, separate repository).

## Decision

Option 2.

- The release workflow still builds the three symbol archives (same names as ADR-0232) and uploads each as a
  workflow artifact `debug-symbols-<os>` with `retention-days: 90`. They are not downloaded by the publish
  job, not listed in `SHA256SUMS`, not attested and not attached to the release.
- v2.0.0-rc.4 (already published) had its three archives deleted by hand and `SHA256SUMS` replaced with a
  list of the four packages. The new `SHA256SUMS` is therefore not covered by that run's attestation; the
  packages themselves still are.
- Releases up to v2.0.0-rc.3 keep their archives.

## Consequences

- The release lists only the installers and `SHA256SUMS`; the release notes no longer explain the archives.
- Symbols of a release are available for 90 days after the release run (Actions › the run › Artifacts).
  A crash report of an older build cannot be symbolized unless someone saved the archives; for the final
  2.0.0 a long-term store (option 3) should be decided separately.
