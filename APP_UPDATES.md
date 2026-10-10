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
Staging/activation require manifest records for both canonical application
executables. Signed versions reject extra unlisted files and unsafe entries
(inventory traversal is bounded to 65536 entries).

Limits: payload 4 MiB, 4096 files, 8192 ranges, aggregate 8 GiB. Files below
8 MiB use whole-file objects; larger files use fixed 4 MiB chunks. Range
transport must be bounded and cancellation-aware; production transport is
not configured. Deterministic offline transport exercises the same pipeline.

The shared first-party `ContentHttps` adapter supports update packs and map
packages without merging their authorization or manifests. An empty source
creates no network backend. `NOVEN_UPDATE_SOURCE_ROOT` defaults to empty;
provisioning requires an approved fixed HTTPS root and public trust key.
Resource source approval and map redistribution rights remain prerequisites
for enabling map downloads. No resource production manifest is configured.

WinHTTP requests retain normal certificate validation, use no proxy/PAC,
disable redirects, cookies and automatic authentication, and send only a
minimal User-Agent/identity encoding/Range. Logical basenames cannot change
the configured origin. Each range is at most 4 MiB; strict 206 Content-Range
and length validation rejects ignored, shifted, truncated or oversized ranges.
Manifest text is bounded to 8 MiB + 2048 bytes. Range streams are hashed by
the requesting service before activation; HTTP headers are not authenticity.
Worker-thread synchronous calls use 1-second resolution and 2-second
connect/send/read timeouts, with a 120-second per-request deadline checked
between calls. Cancellation is observed between finite blocking calls;
handles are never closed concurrently with synchronous WinHTTP operations.
Tests inject an offline backend into this same policy and stream layer.

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

The installer establishes the initial versioned tree and seeds trusted local
inventory after optional code signing. The stable AppId, per-user privileges
and separate User Data root are unchanged. Existing Phase 10 flat files are
left as recovery content while the initial versioned tree is added; shortcuts
switch to Launcher. Trusted uninstall cleanup checks version inventory and
does not touch User Data. Unknown/corrupt program inventory is not deleted.
Future installer baseline-version migrations require separate validation;
v1's tested migration is a flat 0.1.0 installation to versioned 0.1.0.

The offline Review build has separate test-only keys/source and persistent
test User Data outside real LocalAppData. It demonstrates 0.1.0 -> 0.1.1
and trusted local rollback, with optional synthetic map delivery still
available. Real installer lifecycle and user UI acceptance remain separate
from automated tests. Production endpoints, public-key provisioning and
map redistribution clearance are not supplied by this milestone.
