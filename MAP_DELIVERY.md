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
Only `/repos/lingyunalingyun/Noven-Update-Channel/releases/assets/<id>` is used.
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
are never retried as if valid; partial body failures resume through ResourceService.

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
* Shebuka/Tarkov.dev SVG-derived layouts (Factory, Customs, Woods, Lighthouse,
  Shoreline, Reserve, Interchange, Streets, Ground Zero/Terminal groups): further
  verification required for actual proposed use, CC BY-NC-SA 4.0 conditions,
  attribution/share-alike and upstream usage restrictions. No blanket approval.
* Satellite imagery and handbook icons: no confirmed redistribution permission;
  not approved for public publication.
* Laboratory/Maze (Tarkov.dev) and Icebreaker (TarkovBOT.eu) groups: further
  verification required; author/source labels alone are not permission.
* Repository MIT icon material: preserve exact MIT notices, but separately
  verify underlying game/third-party image rights; no blanket image approval.

No real third-party map package is approved in this milestone. Production catalog
must therefore remain empty. A separately downloaded asset has exactly the same
redistribution obligations as a bundled asset. Do not populate it with synthetic
test maps to suggest that actual maps are available.
