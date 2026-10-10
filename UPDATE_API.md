# Application Update v1

Application updates, optional resources and the plugin Registry are separate
trust domains. Production update/resource endpoints remain unconfigured. No
release or map redistribution authorization is implied by this implementation.

## Signed release contract

The bounded JSON envelope contains `scheme: "ecdsa-p256-sha256"`, `keyId`,
`payloadHex` and `signatureHex`. Hex uses lowercase. The signature is the CNG
64-byte P-256 signature over SHA-256 of the exact bytes
`Noven.ReleaseManifest.v1\n` followed by the UTF-8 payload. JSON is not reparsed
or reserialized before verification. Keys come exclusively from the local
first-party trust configuration, never from the envelope or another manifest.
Unknown keys/schemes, unsigned content and modified content fail closed.

Public keys use `BCRYPT_ECCPUBLIC_BLOB` (P-256, 72 bytes); runtime verification
never accepts private-key blobs. Automated tests generate ephemeral test-only
keys in memory. A production signing key must be supplied by the release
operator; no production private key is created, committed or distributed.
Manifest authentication is **not Windows Authenticode**. Unsigned Windows
application/installer binaries can still cause SmartScreen warnings.

The authenticated payload has `schemaVersion: 1`, `releaseVersion`,
`channel: "stable"`, `publishedAt`, optional `notes` and `files`. Stable v1
releases accept strict numeric SemVer triplets, not prereleases/build aliases.
Each file has `path`, `size`, `sha256`, `strategy` and `content`. Each content
range has `offset` (in the reconstructed file), `size`, `sha256`, `pack`
(a relative `.pack` basename) and `packOffset`. Paths are ASCII relative paths
with no traversal, Windows devices, ADS, absolute paths or case-folded
duplicates/file-directory collisions. Unknown optional fields are data only.

Limits: payload 4 MiB, 4,096 files, 8,192 ranges, total release 8 GiB,
paths 240 bytes, pack names 128 bytes, notes 4 KiB. Files below 8 MiB use one
whole-file object; larger files use contiguous fixed 4 MiB chunks (last chunk
may be shorter). Every final file also has a whole-file SHA-256.

## Delta planning

Only an authenticated manifest can enter the planner. Target must be newer
than the current version. Local reuse is never inferred from timestamps:
file/chunk hashes authorize reuse. Verified cache objects are addressed by
SHA-256 and rehashed before reuse; poisoned cache is not reusable. The plan
reports full target bytes, reused bytes, missing download bytes and a
conservative disk reservation (new version, missing cache, largest-file
reconstruction margin). The existing version is not changed or deleted.

Optional map delivery remains under ResourceService and its own bounded
manifest/package validation. An application release cannot authorize a map
package, plugin permission or plugin code execution. Public map delivery stays
disabled until both an approved HTTPS endpoint and redistribution rights exist.
