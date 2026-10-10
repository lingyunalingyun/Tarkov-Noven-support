# Application Update v1

Production update networking and trust provisioning are unconfigured. No
public endpoint is assumed, no releases are published by the tooling, and
startup checks do nothing in this state. Optional map delivery remains a
separate ResourceService trust domain, also production-unconfigured.

## Layout and trust

The per-user Program root contains installer-owned `NovenLauncher.exe` and
`NovenUpdater.exe`, immutable `versions/<SemVer>/` application payloads,
`initial.json`, `current.json`, `last-good.json`, and authenticated release
envelopes in `releases/`. User Data remains separate, including plugins,
grants, history, settings and optional maps. Bootstrap upgrades require an
installer; application releases cannot replace bootstrap files.

The initial installer inventory records every file size/SHA-256. Subsequent
versions require authenticated release metadata and matching final hashes.
This assumes the current user's local Program root is trusted; native plugins
are not an OS sandbox and same-user filesystem tampering is not prevented.

## Release Manifest v1

The outer JSON envelope contains `scheme: "ecdsa-p256-sha256"`, `keyId`,
`payloadHex`, and `signatureHex`. Windows CNG verifies a 64-byte raw P-256
ECDSA signature of SHA-256 over `Noven.ReleaseManifest.v1\n` followed by the
exact UTF-8 payload bytes. Only a 72-byte CNG ECC public blob is embedded.
Unknown keys/schemes, missing signatures and modified payloads fail closed.
This is **not Windows Authenticode**; unsigned Windows binaries may still
produce SmartScreen warnings.

Payload fields: `schemaVersion: 1`, strict numeric `releaseVersion`,
`channel: "stable"`, `publishedAt`, optional plain-text `notes`, and `files`.
Each file has a safe ASCII relative `path`, `size`, `sha256`, `strategy`
(`file` or `chunks`) and `content` ranges containing `offset`, `size`,
`packOffset`, `sha256`, and a local logical `.pack` basename. No commands,
URLs, delete actions, or permission grants are interpreted from metadata.
Paths reject traversal, device names, alternate streams, case-insensitive
duplicates and conflicting file/directory identities. Versions have no
prerelease/build suffix in v1. Normal updates cannot downgrade.

Limits: payload 4 MiB, 4096 files, 8192 ranges, aggregate 8 GiB. Files below
8 MiB use whole-file objects; larger files use fixed 4 MiB chunks. Range
transport must be bounded and cancellation-aware; production transport is
not configured. Deterministic offline transport exercises the same pipeline.

## Planning, staging and activation

SHA-256, not timestamps, decides reuse. Matching whole files and same-offset
local chunks are copied to a clean staged version; verified cache objects
may also be reused. Only missing ranges are requested. Content cache lives
under User Data `updates/cache/`; interrupted partials are scoped to the
authenticated manifest identity and verified before publication. Disk space
estimation reserves target size, missing downloads and reconstruction space
without deleting the current version.
The cache has a conservative 16 GiB / 32768-entry limit, including partials.
Exhaustion fails before transfer without deleting active versions or user
data. v1 retains verified objects rather than performing automatic eviction.

All target files are assembled and whole-file verified before the version
directory is published. The old active version is never patched in place.
Activation revalidates both versions and refuses while Noven/PluginHost is
running. User explicitly chooses Restart and Update. `current.json` switches
atomically, retaining the previous version. A boot token is acknowledged after
application initialization. Failed/unconfirmed startup permits trusted local
rollback on the next launch; intentional exit is not treated as a crash.
Rollback changes Program activation only, never User Data.

Settings exposes check/download/pause/resume/cancel/restart/rollback. Startup
checks are nonblocking and at most once per 24 hours. No automatic install or
restart. Cache is hash-verified, not trusted solely by its name. Older versions
are retained in v1 rather than aggressively reclaimed.

## Offline signing tooling

Build developer-only `NovenReleaseTool` explicitly. It is not installed.
`--release <staged version root> <NEW artifact directory> <version> <keyId>
<operator private CNG blob> <publishedAt>` emits a deterministic manifest
payload, one pack and a signed `release.json`. Signing is randomized; payload
and pack ordering are deterministic. A failed tool run may leave partial
output but never publishes anything. Keep private keys outside source and
runtime payloads; never commit passwords or production signing material.

`--review-key <NEW directory>` generates explicitly non-production
`review-only-p256` fixture keys. Production CMake configuration rejects
review-key identities. Review builds require explicit test configuration and
cannot be packaged through the production install rules.

Final installer migration, real offline executable review and complete
regression are required before claiming Phase 11 completion. User manual
acceptance is separate from automated tests.
