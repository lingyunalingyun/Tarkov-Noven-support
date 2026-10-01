# Official Tarkov.dev marker images

地图标记来自 the-hideout/tarkov-dev 的 `public/maps/interactive`，映射遵循
`src/pages/map/map-images.mjs`。物资分类图标来自官方 regular/items 中
handbookCategories.imageLink。资源驻留本地，运行时不下载图标。
Marker images come from `public/maps/interactive` in the official tarkov-dev
repository, following its `src/pages/map/map-images.mjs` aliases. Loot category
images use official regular/items handbookCategories.imageLink metadata.
All assets are bundled locally; rendering never downloads images.

- Repository: https://github.com/the-hideout/tarkov-dev
- Source identity and hashes: `manifest.json`
- Generator: `tools/maps_generator/generate_marker_images.py`
- Offline reproduction uses the development-only `build/map-icon-sources` cache.
- Conversion: RGBA PNG with transparent square padding to preserve aspect ratio;
  no resizing, redrawing or AI generation.
- Multiple crate/body types share icons exactly as upstream does.
- Hidden extracts use the official PMC extract icon; Easter eggs use the official
  generic loot icon because upstream has no dedicated asset for these UI groups.

Included repository license: `LICENSE.tarkov-dev.txt` (MIT, Copyright 2019
Oskar Risberg). Retain the upstream copyright/license notice. API handbook
images depict Escape from Tarkov content; game imagery rights remain with
Battlestate Games and respective owners. Repository licensing is not a claim
that all game-derived imagery is independently MIT-licensed. These marker
assets are distinct from the Shebuka SVG map backgrounds and their CC BY-NC-SA
4.0 terms. Application source licensing does not override third-party rights.
