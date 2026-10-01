# Interchange map attribution

Original map: **Shebuka**, [tarkov-dev-svg-maps](https://github.com/the-hideout/tarkov-dev-svg-maps).
Source: https://assets.tarkov.dev/maps/svg/Interchange.svg

Licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
The upstream project additionally prohibits use in cheats, hacks, or other tools
that provide unfair advantages in Escape from Tarkov. Preserve this attribution
and upstream terms when redistributing these assets. These assets are separate
from the application's source-code license.

Changes: development-time rasterization into three PNG previews and three local tile packs, retaining
the ground group plus only the selected upper-floor group. No geometry changes.
`manifest.json` records source identity, renderer and hashes; `reference.json`
records upstream projection/layer configuration. Runtime uses local PNG previews
and independently compressed PNG detail tiles; no runtime rasterization or network fetch.

后续卫星底图来源为 DEV reference.json 的 tilePath，使用同一世界投影裁剪本地 atlas。
`satellite.manifest.json` 记录源瓦片 SHA256；上游未覆盖的边缘保持透明，内部缺图拒绝生成。
游戏截图素材权利属于 Battlestate Games 及各自权利人，不把所有卫星素材自动宣称为 SVG 的许可。
Later satellite imagery uses DEV's recorded tilePath, cropped to the same world
projection. `satellite.manifest.json` records source tile hashes. Missing upstream
edge coverage remains transparent; interior gaps reject generation. Game imagery
rights remain with Battlestate Games and respective owners; do not automatically
apply the SVG license to all satellite imagery. Runtime remains local-only.
