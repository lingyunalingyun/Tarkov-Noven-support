# Third-party notices / 第三方来源与版权

Phase 10B 正常 Core 安装包不再携带可选地图图片/缩放包。以下地图来源记录继续保留，不代表下载资源已获授权；生产资源下载尚未配置。安装包中的来源说明位于 docs/maps。
Phase 10B normal Core excludes optional map imagery/zoom packs. Historical provenance below is retained, not redistribution clearance. Production resource downloads remain unconfigured; packaged provenance text is under docs/maps.

## Public event sources / 公开活动来源

活动内容的可选机器译文由 [MyMemory](https://mymemory.translated.net/) 提供，并标明为机器翻译。
仅向该服务发送公开活动标题、正文及社区摘要；不发送 EFT 日志、扫描记录或账户信息。
译文独立缓存在本地，原文修改才需重新翻译；译文不改变来源事实和游戏内容关联。
Optional event machine translations use MyMemory and are labelled as machine translations. Only public
event titles, descriptions and community summaries are sent, never EFT logs, scans or account data.
Translations are cached separately and invalidated by original text changes, without changing source facts or associations.

官方活动事实来自 [Escape from Tarkov Official 英语公开频道](https://t.me/s/escapefromtarkovEN)。
完整名称/身份补充复用 [Tarkov.dev](https://tarkov.dev/api/) 生成目录；
配置变化证据来源为 [Tarkov Silent Changes](https://changes.tarkov-changes.com/latest)，且仅附加官方明确链接的记录。
运行时只保存必要的规范化内容、来源链接与身份，不镜像完整频道或网页。来源内容、游戏名称及商标
权利归各自权利人，署名不等于获得额外再分发授权。Noven 为独立工具，与 Battlestate Games 无官方隶属或背书关系。
Official event facts originate from the public English EFT channel. Entity enrichment reuses Tarkov.dev
catalogs; configuration evidence uses explicitly linked Tarkov Silent Changes records only. Runtime storage
contains necessary normalized content, links and identities, not mirrored channels/pages. Source content,
game names and trademarks retain their respective owners' rights; attribution grants no additional redistribution
permission. Noven is an independent tool, not officially affiliated with or endorsed by Battlestate Games.

本文件记录 Noven 打包地图资源的署名及权利边界，不授予额外权利，不替代第三方原有许可，也不改变应用源码的许可。免费提供或公开源码本身不等于满足素材许可。
These notices preserve third-party rights and licenses; they do not grant additional permissions or change the application source license. Free distribution or public source alone does not establish asset-license compliance.

## Community event supplement / 社区活动补充

社区活动摘录来自 [Escape from Tarkov Wiki](https://escapefromtarkov.fandom.com/wiki/Events)
及其贡献者；以 MediaWiki 公开 API 当前列表补充官方来源，界面明确标为社区资料。
Wiki 的 API rightsinfo 声明 [CC BY-NC-SA](https://www.fandom.com/licensing)；不猜测未声明的许可版本。
修改仅为移除标记、提取当前条目及相关章节要点，不下载/打包图片，不镜像全文。
相应内容及本项目享有版权的改编贡献保留署名、非商业和相同方式共享条件，
不改为应用源码许可；再分发须保留来源链接与许可说明，游戏素材原有权利不变。
Community excerpts credit Escape from Tarkov Wiki contributors. The public MediaWiki current list
supplements official sources and is explicitly labelled community information. API rightsinfo declares
CC BY-NC-SA via the linked Fandom licensing page; no unspecified license version is assumed.
Changes remove markup and extract current entries/section highlights without packaging images or mirroring pages.
These excerpts and copyrightable Noven adaptations retain attribution, noncommercial and share-alike terms,
not the application source license. Redistribution must retain source/license notices and underlying game rights.

## All-map resources / 全地图素材

来源、作者、原始字节与输出哈希逐项记录在 `assets/maps/all.manifest.json` 和每层的 `.manifest.json`。
Per-source authorship, original-byte hashes and output hashes are recorded in the all-map and per-floor manifests.

本次使用 DEV 配置的默认地图：工厂、海关、森林、灯塔、海岸线、储备站、立交桥、街区、震中、终端的布局作者为 **Shebuka**；实验室、迷宫标注 **Tarkov.dev**；破冰船标注 **TarkovBOT.eu**。作者字段描述布局来源，不单独证明卫星图片作者或再分发许可。夜间工厂、21级震中和黑暗实验室复用对应布局，但保留各自 API 身份。教程震中没有已核实的布局，不伪造底图。
Selected DEV layouts credit Shebuka, Tarkov.dev and TarkovBOT.eu as listed above. Layout credits do not establish separate satellite-image authorship or redistribution permission. Alternate map identities retain their own API data; missing verified layouts are not fabricated.

SVG 衍生图按下述 SVG 仓库许可处理；地图瓦片及 handbook 图片不自动继承该许可或应用源码许可。修改包括按 SVG bounds 裁剪、楼层组合、瓦片拼接、逻辑网格重采样、PNG 预览及分块存储。原始输入亦随来源账本保存，不抹去第三方声明。
Changes include SVG-bound cropping, floor composition, tile assembly, logical-grid resampling, PNG previews and tiled storage. SVG-derived assets retain the upstream SVG terms; satellite and handbook images do not automatically inherit them or the application's source license.

## Interchange SVG / 商场矢量底图

作者 / Creator: **Shebuka**. Source: [tarkov-dev-svg-maps](https://github.com/the-hideout/tarkov-dev-svg-maps), [original Interchange.svg](https://assets.tarkov.dev/maps/svg/Interchange.svg).

许可 / License: [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/), [upstream license text](https://github.com/the-hideout/tarkov-dev-svg-maps/blob/main/LICENSE.md). 保留原作版权、署名、许可及免责说明；作品按许可中的免责条款提供，不作担保。

本地修改：栅格化为 PNG 预览及分块包，按楼层保留地面与指定上层；透明 `.overlay` 仅保留对应上层，不重复地面；未改变几何。这些 SVG 衍生资源保留上述许可；涉及本项目享有版权的改编贡献时，亦按 CC BY-NC-SA 4.0 提供。再分发须遵守非商业、署名、标明修改及适用的相同方式共享条件，保留已有修改记录。
Changes: rasterized previews/tiles with selected floor groups; upper-floor overlays omit the ground; geometry unchanged. SVG-derived assets retain this license, including any copyrightable adaptation contributions by Noven.

上游 [README 的使用声明](https://github.com/the-hideout/tarkov-dev-svg-maps#license--use-restrictions) 另外禁止用于作弊、黑客工具或造成不公平优势的软件，并声明违反时撤销许可。此为上游单独声明，须连同原文保留；不将其混写为标准 CC 条款，也不自行判断其法律效力。

## Satellite imagery / 卫星底图

来源：Tarkov.dev 的 [Interchange 配置](https://github.com/the-hideout/tarkov-dev/blob/main/src/data/maps.json) 中的 `tilePath`：`https://assets.tarkov.dev/maps/interchange/main/{z}/{x}/{y}.png`。本地拼接、旋转投影裁剪为 `Satellite.png` / `Satellite.tiles`；来源及哈希见 [satellite.manifest.json](assets/maps/interchange/satellite.manifest.json)。

游戏图像版权属于 Battlestate Games 及各自权利人。现有证据未确认这些卫星瓦片的单独再分发许可或独立图像作者；托管来源、地图配置中的 Shebuka 署名及 SVG 的 CC 许可不能替代该证明。其许可状态保持待确认，公开发布前须取得适用授权证据，或从发布包排除未获授权的素材。
Game-imagery rights remain with Battlestate Games and respective owners. A separate satellite redistribution grant and imagery creator have not been established; neither the SVG license nor hosting proves permission. Verify authorization before public distribution or exclude unlicensed assets.

## Marker and handbook images / 标记与物资分类图标

来源：the-hideout/tarkov-dev 的 `public/maps/interactive`，别名遵循 `src/pages/map/map-images.mjs`；另有 [regular/items](https://json.tarkov.dev/regular/items) 的 `handbookCategories.imageLink` 游戏图片。逐文件来源及哈希见 [icons/manifest.json](assets/maps/icons/manifest.json)。修改仅为 RGBA PNG 转换及透明方形留白，未缩放或重绘。

仓库许可：MIT，**Copyright (c) 2019 Oskar Risberg**；完整原文（含授权条件与免责条款）保留于 [LICENSE.tarkov-dev.txt](assets/maps/icons/LICENSE.tarkov-dev.txt)，再分发仓库来源素材时须一并保留。该仓库许可不证明所有游戏衍生图片均被独立授予 MIT，也不适用 SVG 的 CC 许可。游戏素材及其他第三方原有权利、许可仍保留；未确认的图片授权同样须在公开发布前核实。
Retain the full MIT notice for repository-sourced material. Repository licensing does not establish independent MIT permission for all game-derived images; icons are separate from the CC-licensed SVG maps.

## Rights and evidence / 权利与证据

Escape from Tarkov 游戏内容、名称及商标的相关权利归 Battlestate Games 及各自权利人；本项目不因署名而获得背书或额外授权。记录来源不等于取得再分发许可。详细官方证据、核对日期、已知缺口及集成要求见 [docs/MAP_ATTRIBUTION.md](docs/MAP_ATTRIBUTION.md)。随资源保留 [Interchange SOURCE.md](assets/maps/interchange/SOURCE.md)、[icons SOURCE.md](assets/maps/icons/SOURCE.md)、清单及第三方许可原文。
