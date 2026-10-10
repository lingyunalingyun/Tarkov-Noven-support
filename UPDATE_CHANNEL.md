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

No private-key upload, source push, optional maps, ratings, marketplace package
installation or unrelated work is part of this workflow. Historical AV and
pending installer lifecycle acceptance remain recorded in INSTALLATION.md.
