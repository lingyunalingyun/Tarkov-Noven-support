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

## Paths / 路径

## NVR1 package / 资源包

The v1 package is a bounded regular-file container, not an executable archive: ASCII `NVR1`, uint32 little-endian resource-ID length, resource-ID bytes, uint32 entry count; each entry is uint32 path length, uint64 byte length, ASCII relative path, then raw file bytes. At most 30,000 entries; paths at most 240 bytes; files at most 512 MiB; total materialized bytes must exactly equal installedSize. No compression or executable/symlink entry type is provided in v1.

Only `maps/` relative paths with PNG/tile/JSON/SVG/Markdown/text suffixes are accepted; Windows reserved names, path traversal, drive/UNC paths, alternate streams and case-insensitive duplicate destinations reject. Staging/ancestors reject reparse points. Package size and SHA-256 are checked before extraction from the same write-denying file handle. Extracted files are flushed and the generation directory is atomically published; activation is a separate service operation. Verification reconstructs the original package SHA-256 from installed bytes/index, rather than trusting editable local receipt hashes alone. Synthetic test packages contain no third-party map assets.

AppPaths provides user-root-relative `resources/manifests`, `resources/maps`, `resources/staging`, and `downloads/cache`. They are separate from Program Root in installed/test modes and from plugins, plugin storage and user history. Development mode retains the existing explicit development root. Path accessors do not create directories or delete anything.

## Release boundary / 发布边界

ResourceSelection computes totals and selections by stable resource ID without changing runtime state. ResourceOnboarding gates display on Installed mode only, reads at most 4 KiB, and stores `schemaVersion: 1, completed: true` in `resources/manifests/onboarding.json`. Both Later and successful selection should call Complete. Atomic temporary-file flush/replacement preserves previous state on write failure; corrupt state is not treated as completed. Development/Test modes never automatically request onboarding. This model does not itself display a dialog or enqueue downloads.

Moving map assets out of Setup does not establish redistribution permission. Existing provenance and unresolved rights remain applicable. This foundation alone does not remove maps from Setup, provide first-run UI, download/install resources, or alter uninstall/data retention behavior; those require subsequent verified integration units.
