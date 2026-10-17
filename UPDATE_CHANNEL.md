# Stable external beta update channel

This is an explicitly enabled small external beta, not a general launch.
The approved repository is `lingyunalingyun/Noven-Update-Channel`, branch `main`.
The fixed HTTPS root is:
`https://raw.githubusercontent.com/lingyunalingyun/Noven-Update-Channel/main/stable/`.
Default builds remain offline/unconfigured; configure beta builds with
`cmake -C cmake/NovenStableBeta.cmake ...` in a fresh build directory, or pass
its three public settings explicitly when reconfiguring an existing directory.
Both Main and installer-owned Updater link the same `NovenUpdates` trust library.
Map/resource production downloading remains disabled.

## Trust provisioning

Operator key ID: `noven-stable-2026`. Public CNG P-256 blob SHA-256:
`21B627F9EF8136F48545F22881A1EAF2D409D257B7A66402279E0973EAE825C0`.
Only public material is in the beta preset. The private 104-byte CNG blob stays
outside Git, protected by an operator-only NTFS ACL. Create a protected parent
directory before invoking `NovenReleaseTool --operator-key <NEW child> <keyId>`;
the tool refuses existing destinations and review/test key IDs. No private key
is embedded or printed. Keep a separately protected encrypted backup; a
current-user DPAPI backup depends on that Windows account's recovery material
and is not an independent disaster-recovery backup. Arrange an offline backup
before relying on this key for long-term distribution.

ECDSA P-256/SHA-256 release signatures are not Windows Authenticode. Current
unsigned binaries/installers can still produce SmartScreen warnings.

## Hosting protocol and freshness

Use anonymous HTTPS, normal certificate validation, identity encoding, and
no redirects/cookies/automatic authentication. Update traffic may use the
user's static Windows proxy, but never PAC/WPAD or environment-variable proxies.
The direct-only original transport encountered WinHTTP timeout 12002 on the
operator's network; curl using a proxy is not evidence that direct WinHTTP works.
The native static-proxy backend must pass its own Range and resume checks.

GitHub Raw advertises `Cache-Control: max-age=300`. A mutable `release.json`
can remain stale after publication; never assume immediate propagation or
report acceptance until exact public manifest bytes match the signed artifact.
Old manifests only select old immutable packs; caching does not authenticate
content. Pack filenames are version-unique and never overwritten.
Git repositories reject individual files larger than 100 MiB; the script uses
a conservative strictly-under-100-MiB check. If Core exceeds this, extend the
existing bounded multi-pack manifest rather than switch to whole-Setup updates.
Do not use LFS pointer content as packs. GitHub Releases asset endpoints may
redirect and are not drop-in replacements for this no-redirect protocol.

Changing origins later requires a trusted app/bootstrap release containing the
new fixed root (and approved key rotation where needed). A remote manifest
cannot choose a new origin/key or run migration commands. Retain the old channel
until existing clients have migrated. Public hosting is not a bandwidth/SLA
guarantee; confirm the roommate's network can reach it.

## Operator workflow

### 0.1.3+ startup policy and immutable history

Installed 0.1.3+ requires successful online signature verification at every
launch and permits at most five older patches in the same major/minor line.
Cross-major/minor releases require updating; rollback does not waive admission.
No connection, invalid signature, a missing historical manifest or a Core hash
mismatch blocks normal features, but leaves retry/update/exit available.
Development and explicit test/review roots remain isolated from this policy.

Retain `stable/release-<version>.json` permanently for every published release,
alongside immutable packs. The publish script archives both the previous signed
latest and the new release **before** switching `release.json`, and refuses a
different signed payload under an existing version. `NovenUpdateChannelTool
--archive <signed envelope> <public blob> <keyId> <history directory>` validates
signatures before adding history; it never reconstructs hashes from untrusted
local bytes. Backfill any older available signed envelopes from the channel's
Git history with this tool, without re-signing or rewriting old installers.
An installer-only historical version without an authentic signed envelope
cannot be retroactively declared authenticated by inventing a hash list.

Fresh Setup and delta payload bytes must match the same published signed file
inventory. Apply Authenticode before generating that inventory, never after.
Do not distribute 0.1.3 before its own signed catalog and immutable history are
publicly verified: a locally newer installed build correctly refuses an older
public latest. This source change does not publish 0.1.3 or alter existing
0.1.0/0.1.1/0.1.2 binaries. Older distributed clients cannot be retroactively
forced to adopt startup admission.

GitHub Raw's five-minute caches can delay a new policy taking effect. A signed
static manifest authenticates bytes, not absolute real-time freshness; it is
not an online challenge/response or an unbypassable anti-tamper service. The
mandatory check is at launch, not a continuous connectivity monitor. Core
hash verification excludes installer-owned Launcher/Updater, plugins, optional
resources and user data. No user content, credential or machine ID is sent.

1. Set the canonical CMake product version; make the intended visible change.
2. Configure an MSVC x64 Release build with the beta preset's public settings.
3. Clean/full build, complete CTest, generators/localization, and payload audits.
4. Prepare/sign offline (private key path is explicitly operator-supplied):

```powershell
./scripts/publish_update.ps1 -BuildDirectory E:/build/Release `
  -OutputDirectory E:/releases/0.1.1-validation `
  -PrivateKey C:/protected/operator/update.private `
  -PublicKey C:/protected/operator/update.public -CMake cmake
```

5. Audit the generated artifacts, key exclusion, and exact license material.
6. To publish, invoke the same command with a **new** output directory and
   explicit `-Publish`. The script clones/pushes only the approved update repo,
   uploads immutable packs, verifies every remote referenced range against the
   signed local bytes, then publishes `release.json` last. It never pushes source.
7. If propagation checking reports stale manifest, wait for cache expiry and
   verify exact bytes again; do not overwrite packs or re-sign the published
   same-version release to chase caches.
8. Use `NovenUpdateChannelTool --verify <artifacts> <public blob> <keyId>` for
   independent local signature/pack checks and `--verify-remote` with an added
   HTTPS root for all remote pack ranges. Tool targets are excluded from Setup.
9. On an isolated installation of the exact tester Setup, run real Launcher/UI
   check/download/pause/restart/resume/apply/rollback acceptance. The tool's
   `--plan/--interrupt/--resume/--lifecycle` modes exercise native Internet
   transport and updater core with explicit Program/Test roots, but are **not**
   a replacement for installed GUI acceptance. Count actual body bytes across
   interrupted and resumed runs, not just the planner's estimate; TLS/header/
   transport overhead is separate from content byte counts.

The native channel tool defaults to the original `0.1.0 -> 0.1.1` fixture.
For later releases, append explicit baseline and target versions after key ID,
for example `--resume <Program root> <Test root> <HTTPS root> <public blob>
<keyId> 0.1.1 0.1.2`. The isolated active version must match the baseline;
invalid versions and downgrades fail before network access. These operator-only
arguments do not change distributed application or installer bytes.

No private-key upload, source push, optional maps, ratings, marketplace package
installation or unrelated work is part of this workflow. Historical AV and
pending installer lifecycle acceptance remain recorded in INSTALLATION.md.

## Initial beta verification boundary

The genuine signed 0.1.1 payload contains 68,190,100 bytes. Against the configured
0.1.0 staging payload, native HTTPS reused 65,069,972 local bytes and received
3,120,128 content bytes (95.42% avoided). An interrupted process received 65,536
bytes; a new process resumed at that exact offset and received the remaining
3,054,592 bytes. These counts exclude TLS/HTTP overhead. Every referenced remote
pack range was also checked independently before manifest publication.

Core version-store activation, rollback to 0.1.0, reactivation of 0.1.1, and
preservation of isolated user-data sentinels passed. This used the exact tester
installer's CMake staging payload, not an actual Setup installation. Installed
Launcher/UI pause, restart, health confirmation and installer lifecycle acceptance
remain pending in a clean Windows test environment. Roommate manual acceptance
is likewise pending; these core tests must not be reported as GUI acceptance.
