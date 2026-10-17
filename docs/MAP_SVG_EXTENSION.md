# Lab and Labyrinth SVG compatibility audit

Audit date: 2026-10-17. Resource packages remain CC BY-NC-SA 4.0, under
[Noven's permanent free/noncommercial/anti-cheating policy](../PROJECT_POLICY.md).
Software MIT licenses are not used to authorize artwork.

## Pinned sources

| Map | Creator / source revision | Original SVG SHA-256 | Decision |
| --- | --- | --- | --- |
| The Lab (`5b0fc42d86f7744a585f9105`) | Shebuka, [tarkov-dev-svg-maps](https://github.com/the-hideout/tarkov-dev-svg-maps/blob/5a8b6115d1c0cf56f2ebaac1a96fa5ae3074d178/Labs.svg), `5a8b6115d1c0cf56f2ebaac1a96fa5ae3074d178` | `099a9eaabc305431d906151e03c45d6f06bb885ec46234735e93cc2ab02a222a` | License supports companion use; current-layout compatibility fails, withheld |
| The Labyrinth (`6733700029c367a3d40b02af`) | acidphantasm, [SPT-DynamicMaps](https://github.com/acidphantasm/SPT-DynamicMaps/blob/4944764f5f6c42d152dca6bd1b5371c4f6212a9e/Plugin/Resources/Maps/Labyrinth/Layers/Labyrinth.svg), `4944764f5f6c42d152dca6bd1b5371c4f6212a9e` | `6b9448b2c51a61f52d68fcd11de806d0c93653c37f77cb7d5f53103233822c94` | Asset-specific CC license; compatible vector-only resource |

Lab uses the pinned project's LICENSE.md and README.md, including its explicit
anti-cheating statement. Labyrinth has its own
[acidphantasm-LICENSE.md](https://github.com/acidphantasm/SPT-DynamicMaps/blob/4944764f5f6c42d152dca6bd1b5371c4f6212a9e/Plugin/Resources/Maps/Labyrinth/Layers/acidphantasm-LICENSE.md).
The optimized SVG and its matching license were added together by acidphantasm
in `24cac5e5a1c48d010aaa225d86cba2ef3efda1df`; the earlier SVG source was also
added by acidphantasm in `ad040b4efee0cc74ff95b5013a58c0f55d94996b`.
Both complete license files have SHA-256
`1349a4b6148492b44f629e64eed676612e234fe9a839e4f3b277c1482c8849f1`.
NormalMaps' README separately identifies map licenses as distinct from its MIT
software license; no NormalMaps artwork/code is included in this package.

## Geometry and unchanged 0.1.2 contract

The accepted 0.1.2 PvP/PvE map references, floors, extents and points are unchanged.
The native MapPage, MapProjection and ResourceService are unchanged. No application
rebuild, new manifest format, plugin integration or satellite fallback is needed.

Lab SVG groups map as `First_Level -> Base`, `Second_Level -> Second_Level`,
`Technical_Level -> Technical`. Current display orders are 1, 0, 2 respectively;
height selection remains the existing native extents (-0.9 / 3 boundaries plus
the second-level spatial rectangle). All groups render, but the old SVG calibration
uses x [-292,-96], z [-441,-223]. The accepted current projection uses x [-287,-80],
z [-477,-193], rotation 270. Current Hangar Gate (-170.49,-218.87) and Parking Gate
(-231.46,-457.77) lie outside that source extent. Upstream's older Parking Gate
reference is (-231.73,-434.816376), about 23 world units away. Relabeling, stretching,
or retaining the existing markers cannot establish current floor geometry.
There is no approved replacement geometry for those regions in this audit.
Lab is NOT published as complete, and no satellite tiles fill the gaps.

Labyrinth uses one Base floor. Its pinned source calibration is x [-52.5,50.7],
z [-36.5,75.3], rotation 90. The accepted native projection is x [-52,53],
z [-37,76], rotation 270, width 113, height 105. An affine transformation maps
the original SVG into that unchanged native projection (including the 180-degree
orientation difference and small bounds adjustment). No point coordinates change.
Both exits and twelve catalogued switches are within 0.632 world units of the
depicted vector paths; trigger/switch centers need not be path-interior pixels.
This is static reference/geometry validation, not live-game or user GUI acceptance.

Calibration metadata is pinned to the SPT-DynamicMaps revision above. Labs JSONC
SHA-256: `d7929df2f686a01dcd7c13a608c9cc98f8c4e0866a668994828df7ddeeb342ad`;
Labyrinth JSONC SHA-256:
`6633fd2ea1806b8d274c59af22cf88d4d99288f34f111e787b8578c428458841`.

## Reproduction and attribution

`tools/resources/prepare_additional_svg.py --assets <assets> --upstream <pinned
inputs> --output <NEW directory> --version 1.0.0` performs offline preparation.
It checks pinned SVG/config/license hashes, rejects external raster/active content,
checks floor coverage and current exits, and emits deterministic previews/NVTILES1
and NVR1 using the established packager. Upstream input filenames are documented
by its two explicit source records. Withheld Lab previews are not payloads.

The package carries unchanged SOURCE.svg, full LICENSE.txt, SOURCE.md and
PROVENANCE.json. Attribution identifies acidphantasm, the original pinned URL/hash,
upstream project, previous optimized-source history, and Noven's coordinate
conversion/rasterization/tiling. Any copyrightable Noven adaptations use the same
CC BY-NC-SA 4.0. It contains no satellite images, screenshots, unrelated icons,
SPT plugin code, executables or user data. No endorsement is claimed.

Operator `NovenResourceChannelTool --local-package <catalog-input.json> <test root>`
exercises real extraction, native PvP/PvE floor loading, corruption verification,
repair, offline restart and deletion. Network flow modes accept an optional stable
map ID after keyId; production trust, TLS, redirect and hash checks remain intact.

## Deferred maps (no new publication)

* Customs: the current complete floor set still lacks fourth-floor SVG coverage.
* Reserve: upper-floor coverage remains missing; do not label a partial set complete.
* Dark Lab: the upstream default layout shares the Lab map reference, but that does
  not prove current geometry is represented by the old SVG. CC-compliant reuse is
  not barred merely by a different map name, but current layout validation is
  still required; no automatic reuse or new publication here.
* Ground Zero Tutorial: no verified tutorial-specific floor/projection metadata;
  do not infer equivalence merely from the map name.
* Icebreaker: no independently licensed vector alternative established here;
  TarkovBOT.eu/satellite materials remain withheld pending their own evidence.

## Published resource

Labyrinth 1.0.0 is asset **649688513** under the existing `maps-v1` release:
`6733700029c367a3d40b02af-1.0.0.nvr`. Download: **398,241 bytes**;
installed: **397,931 bytes**, six files. Package SHA-256:
`2ce0f5cb577b8db34d73af6a68796799b33704ef372fa1eeb2188c4c6ec425a3`.
`Base.satellite.png` / `.tiles` retain the old client's required filenames;
their content is exclusively the licensed SVG derivative, not satellite imagery.

The production catalog at `maps/manifest.json` is authenticated with
`noven-resources-2026`, ECDSA P-256/SHA-256 in the resource-specific trust domain.
All original ten records and immutable package digests remain unchanged.
Anonymous native WinHTTP verified API 302 -> trusted CDN 206, complete package
bytes and a persisted 65,536-byte pause. A new process resumed at that offset;
an injected disconnect resumed at byte 131,072 without re-fetching the prefix.
Native install/PvP+PvE load/verify/repair/offline restart/in-use protection/delete/
empty-cache redownload passed. Static geometry and native tests do not constitute
user GUI or live-game acceptance.

The preparation tests passed 11/11, map generator tests 45/45, and focused
resource CTest 4/4 in each Debug/Release configuration. Full application builds
and complete CTest were not repeated: only excluded operator tooling and public
documentation changed. The accepted 0.1.2 application and Setup bytes remain
unchanged, and the application update channel was not modified.
