# Resource Delivery v1 / 官方资源交付 v1

此契约是 Phase 10B 基础设施，不是应用更新器。生产下载未配置；没有批准的 HTTPS 来源及相关地图再分发授权前，不发布资源、不开放生产下载。
This Phase 10B contract is not an application updater. Production delivery remains unconfigured until an approved HTTPS source and applicable map redistribution rights are confirmed. No resources are published by this implementation.

## Manifest / 清单

UTF-8 JSON, at most 256 KiB, `schemaVersion: 1`, `components` array at most 64 records. Records are deterministically sorted by resource ID. Unknown optional fields are ignored as data; they never execute commands or grant plugin permissions. Duplicate JSON keys and malformed UTF-8 fail closed.

Each component contains:

- `resourceId`: `maps.<stableMapId>`; stableMapId must match a current MapCatalog identity supplied by the first-party caller, never a localized name.
- `type`: `map`; `required`: false.
- `titleZh`, `titleEn`: nonempty UTF-8 labels, at most 256 bytes each.
- `version`: strict SemVer, at most 128 bytes.
- `downloadSize`: positive integer, at most 2 GiB; `installedSize`: positive integer, at most 8 GiB. These are validation ceilings, not allocation instructions.
- `sha256`: exactly 64 lowercase hexadecimal characters for the downloadable artifact.
- `artifact`: unique bounded filename, at most 128 ASCII bytes, ending in `.nvr`; no paths, percent escapes, traversal, URL or executable extension. The package format/extraction implementation is not established by this parser.

IDs use lowercase ASCII letters, digits and hyphens, at most 64 bytes. Duplicate resource IDs/artifacts reject the manifest. Manifest data does not select a download host. A first-party source policy supplies a fixed HTTPS base directory; default empty policy rejects all download URL construction. Only port 443 is accepted, without query, percent escapes or traversal. No transport or production source is configured in this foundation.

## NVR1 package / 资源包

The v1 package is a bounded regular-file container, not an executable archive: ASCII `NVR1`, uint32 little-endian resource-ID length, resource-ID bytes, uint32 entry count; each entry is uint32 path length, uint64 byte length, ASCII relative path, then raw file bytes. At most 30,000 entries; paths at most 240 bytes; files at most 512 MiB; total materialized bytes must exactly equal installedSize. No compression or executable/symlink entry type is provided in v1.

Only `maps/` relative paths with PNG/tile/JSON/SVG/Markdown/text suffixes are accepted; Windows reserved names, path traversal, drive/UNC paths, alternate streams and case-insensitive duplicate destinations reject. Staging/ancestors reject reparse points. Package size and SHA-256 are checked before extraction from the same write-denying file handle. Extracted files are flushed and the generation directory is atomically published; activation is a separate service operation. Verification reconstructs the original package SHA-256 from installed bytes/index, rather than trusting editable local receipt hashes alone. Synthetic test packages contain no third-party map assets.

AppPaths provides user-root-relative `resources/manifests`, `resources/maps`, `resources/staging`, and `downloads/cache`. They are separate from Program Root in installed/test modes and from plugins, plugin storage and user history. Development mode retains the existing explicit development root. Path accessors do not create directories or delete anything.

## ResourceService / 运行服务

ResourceService owns a single bounded inventory/queue keyed by stable resource ID. Two cancellable worker threads perform download, hashing, extraction, verification and deletion; UI commands only schedule work. There are at most 64 queued jobs, one job per resource, no automatic retries. The injected first-party transport opens a stream with exact expected identity, byte offset and total size; reads are bounded to 64 KiB. It must honor cancellation with bounded waits. No production network transport exists. Offline transports are rejected for Installed mode.

Persistent partial artifacts live only in `downloads/cache/resources/<stableMapId>.part`, with atomic manifest identity metadata alongside. Resume validates version/size/hash and actual file size; mismatched or malformed metadata restarts at zero. Each completed block is flushed. Final size/hash/extraction must pass before atomically replacing `resources/manifests/<stableMapId>.json`. Existing healthy generations remain available on failed replacement. Startup queues actual-byte verification before exposing a map location. Local inventory is not a signature/authenticity guarantee; approved source and redistribution rights remain required.

ProductionEndpointUnconfigured is separate from empty/corrupt manifest and network failure. Known MapCatalog labels remain visible with unknown size until approved metadata exists. Stored installed resources can be verified and used offline without a configured endpoint. First-party UI can acquire a map lease; Delete and replacement requests reject while the resource is in use. Deletion derives its target exclusively from inventory identity, atomically retires its directory, records removal, and cleans only its own retired generation. Default uninstall still preserves all resources/user data. Disk estimates reserve temporary plus extracted size across active jobs; estimates are conservative, not a guarantee against external disk usage.

## Release boundary / 发布边界

ResourceSelection computes totals and selections by stable resource ID without changing runtime state. ResourceOnboarding gates display on Installed mode only, reads at most 4 KiB, and stores `schemaVersion: 1, completed: true` in `resources/manifests/onboarding.json`. The native themed dialog calls Complete for Later and confirmed Download. Atomic replacement preserves previous state on write failure. Development/Test modes do not automatically show onboarding. Settings opens the same native resource manager, whose contextual actions all route to ResourceService; cache clearing runs on a worker and never removes installed resources. Unconfigured builds show catalog labels, not invented sizes or enabled download controls.

Installed MapPage resolves verified resources through ResourceService, never fallback program imagery. Missing maps show a localized unavailable/download state. Development retains its existing explicit asset behavior. Deterministic synthetic tests prove core-only startup and install/load/in-use protection/delete/missing transitions. Moving map assets out of Setup does not establish redistribution permission; unresolved rights still apply. Default uninstall/data retention behavior is unchanged.
