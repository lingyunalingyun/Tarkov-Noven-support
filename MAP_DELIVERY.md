# Authenticated map delivery

Application releases (`stable/release.json`) and optional map resources
(`maps/manifest.json`) are independent trust domains. The latter is a bounded
ECDSA P-256/SHA-256 envelope with `scheme`, `keyId`, `payloadHex` and
`signatureHex`. The digest covers `Noven.ResourceManifest.v1\n` followed by exact
UTF-8 payload bytes. Application-release signatures cannot authorize resources.
Only operator-provisioned resource public keys are trusted; rotating a key needs
a trusted application release that carries the new public key before catalogs
switch. Private keys never enter Git, runtime or Setup. This is not Authenticode.

The signed payload retains Resource Manifest v1 and adds a signed `audience`:
`production` or `synthetic`. Production rejects synthetic catalogs. Records use
existing MapCatalog IDs, `maps.<stableMapId>`, version, positive download and
installed sizes, SHA-256, an immutable `.nvr` basename and signed `githubAssetId`.
The positive integer asset ID binds the exact Release asset in the approved repository;
it is required for remote catalogs and optional only in legacy offline v1 records. No host, command,
absolute path or download URL is supplied by a resource record. Maximum metadata
is 256 KiB before hex encoding, 64 records, 2 GiB per package. Unknown IDs, bad
signatures, bounds and paths fail closed. Optional fields are forward-compatible.

## Fixed hosting policy

Metadata: `https://raw.githubusercontent.com/lingyunalingyun/Noven-Update-Channel/main/maps/`.
Approved production assets: fixed repository Releases tag `maps-v1`.
Original synthetic transport tests use the distinct `phase14-synthetic-v1`
prerelease and `maps-review/manifest.json`; they never populate production maps.

The Releases adapter maps the authorized basename to its signed asset ID and
uses the anonymous GitHub Asset API with `Accept: application/octet-stream`.
Only `/repos/lingyunalingyun/Noven-Update-Channel/releases/assets/<id>` is used
for API resolution. A confirmed anonymous quota response (403 with
`X-RateLimit-Remaining: 0`, or 429) switches this adapter session to the fixed
approved repository/tag and signed artifact basename. It does not keep calling
the exhausted API for each chunk. Non-quota 403 does not trigger fallback.
The basename is already authenticated; final SHA-256, size and safe extraction
remain mandatory even if a repository owner replaces a named asset. Both
resolution routes request no-cache and allow exactly the same single CDN hop.
The initial browser download endpoint passed probes but repeatedly timed out
in the complete native flow; it is not the production resolution route.
The adapter then accepts at most one
HTTP 302 to the exact HTTPS origin `release-assets.githubusercontent.com` and
`/github-production-release-asset/` path. No suffix host matching, downgrades,
userinfo, alternate ports or second redirects. Automatic redirects remain
disabled in WinHTTP. Each new 4 MiB Range resolves the asset again, avoiding
persistence of expiring CDN URLs. Range status, start/end/total and byte lengths
are validated; final package SHA-256 still controls activation. Cookies and
automatic authentication are disabled. Normal Windows certificate validation
and bounded transport timeouts remain enabled: resource DNS 5 s, connect/send/read
10 s each, with a 120 s stream deadline. Initial native service requests hit
12002 with the prior 1/2 s update-oriented limits; these resource-only limits do
not change application-update timeouts. This resource adapter does not
change application updater redirect policy. Explicit static Windows proxy
configuration is supported; no PAC/WPAD or environment-proxy execution.
Transient connection/header failures have at most three attempts with 0.5/1 s
cancellable backoff. Invalid redirects, signatures, hashes and Range responses
are never retried as if valid. A resource stream additionally allows at most two
transient body-read recoveries (12002/12029/12030) over its entire lifetime, with
the same cancellable 0.5/1 s backoff. It closes the failed response, resolves the
signed asset ID again with a no-cache request, and requests precisely the
undelivered remainder of the original Range. Every renewed response must have
the exact 206/Content-Range/length/total. Delivered bytes are not fetched again;
an uncommitted final block may be repeated if its EOF check failed. Timeout
values are unchanged; a response deadline is a typed timeout, not a stale Win32
error. Cancellation, invalid headers, TLS errors, hashes and extraction errors
never bypass validation. Exhausted retries retain resumable partials and report
Error. Application-update streams do not opt into these read retries.

Native diagnostics report request ranges, response/header timings, read time,
retry counts and Win32 failure stage, never CDN query strings or credentials.
`NovenResourceChannelTool --cut-flow` deliberately closes one **real HTTPS**
body after receiving a prefix, then requires automatic Range recovery. The
tool checks same-session empty-cache deletion/redownload and restarted/offline
map load, and reports received body bytes separately from HTTP/TLS overhead.

Deletion uses a short random staging tombstone so an already-valid map tree
does not acquire an unnecessary 64-character prefix. It clears only that
resource's partial artifact and progress, retaining the same trusted-root,
reparse-point and in-use guards. Other user data is untouched.

Mutable Raw metadata can be cached (observed max-age=300); verify published bytes
before announcing availability. Upload immutable Release assets first, check
remote lengths/ranges/hashes, then publish the signed catalog. Never overwrite a
published artifact to repair it; issue a new version/identity. Release assets
avoid committing gigabytes of maps into Git. GitHub hosting is not an SLA.

## Runtime and safety

`App::StartResources()` starts a background catalog worker. The existing
ResourceService owns all inventory, queue, partials, map leases and activation.
Settings distinguishes unconfigured/loading/online/cached/offline/invalid
catalogs. A fetch failure can use the last cached signed catalog only after
re-verification. A bad fetched signature never falls back as if authenticated.
Unpublished maps get no download authorization; installed bytes remain usable
offline. Cached metadata does not bypass final package verification.

Downloads use existing bounded workers, persisted identity-bound partials,
expected size/hash, safe NVR1 extraction and generation-directory activation.
Existing versions remain untouched on failed replacement. A held map lease
prevents deletion/replacement. Deletion targets only the managed stable map
directory, never Program files, plugins, settings, history or logs. Shutdown
cancels catalog/transfer work before service destruction. Verify/Repair and
missing-map UX remain native; no maps return to Core Setup.

## Reproducible packaging and authorization

`tools/resources/package_map.py` requires an explicit source directory and
per-file inventory with source/license identity, `classification` and
`approvalReference`. It rejects unapproved inventories, unsafe names, duplicate
case-insensitive destinations, links/junctions, executable extensions and size
bounds. License/source notices must travel inside the package. Sorted inputs
produce deterministic NVR1 bytes and a catalog entry. The operator attestation
is evidence to review, not an automatic legal authorization mechanism.

Sign the explicit catalog input using:

```text
NovenReleaseTool --resource-sign <catalog-input.json> <NEW manifest.json> <keyId> <operator private CNG blob path>
```

Upload a NEW immutable asset first, validate its complete remote bytes and Range
behavior, add the returned numeric `githubAssetId` to the catalog entry, verify
the signed catalog locally, and publish `manifest.json` last. Never sign an
unresolved/guessed asset identity. Public catalogs must not contain test records.

No private key bytes should be passed on the command line. Review tools are
excluded from installer payload. `create_online_fixture.py` produces original
synthetic PNGs/text at the real Factory stable identity and runtime paths, not
copies of third-party imagery. `NovenResourceChannelTool` verifies signatures,
native HTTPS ranges, pause/process-resume, installation, WIC PNG decoding,
offline restart, in-use deletion denial and subsequent deletion/redownload in
explicit Test roots. Tool success is not user GUI acceptance.

Before publication, run `scripts/audit_signing_secrets.ps1` with the explicit
operator application/resource `-PrivateKeys`, source root and staging/artifact
`-PayloadRoots`. It checks raw/hex/base64 private blobs and scalars without
printing their contents. This supplements, not replaces, the strict payload
allowlist and operator review. The normal installer remains Core-only.

### Current redistribution classification

See `docs/MAP_ATTRIBUTION.md` and retained per-source records; they are not grants.

* Original synthetic PNG/text: approved for isolated non-production tests only.
* Qualifying Shebuka SVG-derived layouts: use the published CC BY-NC-SA 4.0
  grant with the owner's explicit noncommercial/non-cheating policy in
  PROJECT_POLICY.md, verified source hashes and packaged attribution/notices.
  Companion use alone does not require separate written permission. This does
  not clear satellite images, screenshots or independently licensed icons.
* Satellite imagery and handbook icons: no confirmed redistribution permission;
  not approved for public publication.
* Laboratory/Maze (Tarkov.dev) and Icebreaker (TarkovBOT.eu) groups: further
  verification required; author/source labels alone are not permission.
* Repository MIT icon material: preserve exact MIT notices, but separately
  verify underlying game/third-party image rights; no blanket image approval.

Complete SVG-only coverage is available for Factory, Night Factory, Woods,
Lighthouse, Shoreline, Interchange, Streets, Ground Zero, Ground Zero 21+ and
Terminal. Customs/Reserve have satellite-only floors and must not be advertised
as complete SVG maps. Tutorial has no verified layout. Other imagery-only maps
remain withheld. A separate download has the same redistribution obligations as
bundled content; synthetic test maps remain segregated from production.

`tools/resources/prepare_svg_maps.py --assets <assets> --sources <source cache>
--upstream <exact LICENSE.md/README.md directory> --upstream-ref <notice commit>
--output <NEW output> --version <resource SemVer>` prepares only complete
SVG-derived floor sets using the existing NVR1 packager. It checks source and
derived hashes, excludes embedded raster/active SVG content and includes the
unchanged SVG, full license, upstream statement, SOURCE.md and PROVENANCE.json.
It never uploads or treats an unrelated image as SVG-licensed.

Operator `NovenResourceChannelTool --verify-remote <signed manifest> <public blob>
<keyId> production <fixed asset root>` verifies every remote package byte through
the production HTTPS adapter. `--production-interrupt`, `--production-cut-flow`
and `--production-flow` use the production signature audience and complete
MapCatalog labels to test Factory installation, native map load, offline
restart, verification, in-use protection, deletion and redownload. They use
explicit Program/Test roots, never the real user's data. Existing synthetic
modes retain their separate audience. None of these tools are shipped in Core.
